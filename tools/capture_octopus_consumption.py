"""Capture one verified Octopus import meter window for a local CanaryMiner review."""
import argparse
import base64
from contextlib import contextmanager
import datetime as dt
from decimal import InvalidOperation
import getpass
from hashlib import sha256
from hmac import compare_digest
from http.client import HTTPException
import json
import os
from pathlib import Path
import re
from secrets import token_hex
import signal
import ssl
import stat
import subprocess
import sys
import time
from urllib.error import HTTPError
from urllib.parse import urlencode, urlsplit
from urllib.request import HTTPSHandler, ProxyHandler, Request, build_opener
import warnings

# Isolated Python excludes the script directory. Load the verified siblings here.
if __name__ == '__main__' and sys.flags.isolated:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_octopus_rates import (
    EPOCH, HALF_HOUR, MAX_PAGE_BYTES, ONE_SECOND, PAGE_SIZE, NoRedirect, iso_utc, publish, utc,
)
from prepare_octopus_consumption import (
    MAX_BYTES, MAX_ROWS, MILLISECOND, REPARSE_POINT, Number, append_readings, decode_json, encode_bundle,
    local_path, read_page,
)


# The credential destination is independent of the public price client's URL.
ORIGIN = 'https://api.oegb-kraken.energy'
MAX_ANNUAL_ROWS = 18432
MAX_ANNUAL_BYTES = 1792 * 1024
MAX_ACCOUNT_BYTES = 1024 * 1024
PROVIDER_HOSTS = ('api.oegb-kraken.energy', 'api.octopus.energy')
BINDING_NAME = '.canary-octopus-meter.json'
CREDENTIAL_NAME = '.canary-octopus-capture.cred'
CAPTURE_LEASE_NAME = '.canary-octopus-capture.lock'
MAX_CREDENTIAL_BYTES = 16 * 1024
IDENTITY_PROMPTS = (
    ('Octopus account number: ', r'A-[A-Za-z0-9]{1,32}'),
    ('Import MPAN: ', r'[0-9]{13}'),
    ('Meter serial: ', r'(?!\.{1,2}\Z)[A-Za-z0-9_.-]{1,128}'),
    ('Octopus API key: ', r'[!-9;-~]{1,256}'),
)


class CaptureError(ValueError):
    """A diagnostic safe to display without printing credentials or account data."""

class CapturePending(CaptureError):
    """The provider still has fewer readings than the elapsed cycle requires."""


def read_identity():
    if not sys.stdin.isatty():
        raise CaptureError('open an interactive terminal for masked account entry')
    values = []
    with warnings.catch_warnings():
        warnings.simplefilter('error', getpass.GetPassWarning)
        try:
            for prompt, pattern in IDENTITY_PROMPTS:
                value = getpass.getpass(prompt)
                if not re.fullmatch(pattern, value):
                    raise CaptureError('invalid Octopus account entry')
                values.append(value)
        except (getpass.GetPassWarning, EOFError):
            raise CaptureError('masked account entry unavailable') from None
    return tuple(values)


def private_opener():
    # A fresh verified client context does not enable SSLKEYLOGFILE logging.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_default_certs()
    return build_opener(ProxyHandler({}), HTTPSHandler(context=context), NoRedirect())


def remaining_time(deadline):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise CaptureError('Octopus capture deadline expired')
    return min(remaining, 15)


def fetch_json(opener, route, authorization, limit, deadline):
    url = ORIGIN + route
    request = Request(url, headers={'Accept': 'application/json', 'Accept-Encoding': 'identity'})
    request.add_unredirected_header('Authorization', authorization)
    try:
        with opener.open(request, timeout=remaining_time(deadline)) as response:
            if response.status != 200 or response.geturl() != url:
                raise CaptureError('unexpected Octopus response')
            if (response.headers.get_content_type() != 'application/json' or
                    response.headers.get('Content-Encoding', 'identity').lower() != 'identity'):
                raise CaptureError('unexpected Octopus content type or encoding')
            lengths = response.headers.get_all('Content-Length', [])
            if lengths and (len(lengths) != 1 or not re.fullmatch(r'[0-9]{1,12}', lengths[0]) or
                            not 0 < int(lengths[0]) <= limit):
                raise CaptureError('invalid or oversized Octopus response length')
            expected = int(lengths[0]) if lengths else None
            data = bytearray()
            while len(data) <= limit:
                chunk = response.read1(min(16384, limit + 1 - len(data)))
                remaining_time(deadline)
                if not chunk:
                    break
                data.extend(chunk)
            if len(data) > limit or (expected is not None and len(data) != expected):
                raise CaptureError('incomplete or oversized Octopus response')
    except HTTPError as error:
        error.close()
        if error.code in (401, 403):
            raise CaptureError('Octopus refused account authorization') from None
        if error.code == 429:
            raise CaptureError('Octopus rate limited the capture') from None
        raise CaptureError('Octopus returned an unsuccessful HTTP response') from None
    except (OSError, HTTPException):
        raise CaptureError('could not read the verified Octopus HTTPS endpoint') from None
    try:
        return decode_json(data)
    except (ValueError, RecursionError):
        raise CaptureError('invalid Octopus response data') from None


def covers_window(property_data, begin, end):
    try:
        start = dt.datetime.fromisoformat(property_data['moved_in_at'].replace('Z', '+00:00'))
        closing = property_data['moved_out_at']
        finish = dt.datetime.fromisoformat(closing.replace('Z', '+00:00')) if closing is not None else None
        return start.utcoffset() is not None and start <= begin and (
            finish is None or (finish.utcoffset() is not None and end <= finish))
    except (AttributeError, KeyError, OverflowError, TypeError, ValueError):
        return False


def records(document, key):
    rows = document.get(key) if isinstance(document, dict) else None
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise CaptureError('invalid Octopus account response')
    return rows


def validate_private_alias(scope, identity):
    alias = scope.casefold()
    for value in identity:
        value = value.casefold()
        # Short serials may occur inside unrelated words. Reject them as whole labels.
        found = value in alias if len(value) >= 4 else re.search(
            r'(?<![a-z0-9])' + re.escape(value) + r'(?![a-z0-9])', alias)
        if found:
            raise CaptureError('choose a private alias separate from the supplier identifiers')


def binding_digest(salt, identity, scope):
    # The key is deliberately excluded so it can be rotated. The salt stays local.
    material = '\0'.join((*identity[:3], scope)).encode('ascii')
    return sha256(bytes.fromhex(salt) + material).hexdigest()


def check_binding(path, identity, scope):
    try:
        saved = read_page(path)
    except FileNotFoundError:
        return False
    except (OSError, ValueError, RecursionError):
        raise CaptureError('could not read the local Octopus meter binding') from None
    if (not isinstance(saved, dict) or set(saved) != {'schema_version', 'salt', 'digest'} or
            type(saved['schema_version']) is not Number or saved['schema_version'] != '1' or
            any(type(saved[field]) is not str or not re.fullmatch(r'[a-f0-9]{64}', saved[field])
                for field in ('salt', 'digest'))):
        raise CaptureError('invalid local Octopus meter binding')
    if not compare_digest(saved['digest'], binding_digest(saved['salt'], identity, scope)):
        raise CaptureError('the meter or alias differs from the local workspace binding')
    return True


def pin_binding(path, identity, scope):
    if check_binding(path, identity, scope):
        return
    salt = token_hex(32)
    pin = {'schema_version': 1, 'salt': salt, 'digest': binding_digest(salt, identity, scope)}
    try:
        publish(path, (json.dumps(pin) + '\n').encode('ascii'))
    except FileExistsError:
        # Another publisher won the first-use race. Its binding must agree.
        if not check_binding(path, identity, scope):
            raise CaptureError('could not confirm the local Octopus meter binding') from None


def confirm_destination(output, parent_state):
    current_parent = local_path(output).parent.stat()
    if not os.path.samestat(parent_state, current_parent):
        raise CaptureError('the destination workspace moved during capture')


def verify_import_meter(data, account, mpan, serial, begin, end):
    if not isinstance(data, dict) or type(data.get('number')) is not str or data['number'] != account:
        raise CaptureError('unexpected Octopus account response')
    points = [point for prop in records(data, 'properties') if covers_window(prop, begin, end)
              for point in records(prop, 'electricity_meter_points')
              if type(point.get('mpan')) is str and point['mpan'] == mpan]
    if len(points) != 1 or points[0].get('is_export') is not False:
        raise CaptureError('could not verify one account import meter for the complete window')
    meters = [meter for meter in records(points[0], 'meters')
              if type(meter.get('serial_number')) is str and meter['serial_number'] == serial]
    if len(meters) != 1:
        raise CaptureError('the selected meter serial is unavailable or ambiguous')


def matches_page_link(link, route, required):
    if not required:
        return link is None
    if type(link) is not str or not 1 <= len(link) <= 4096:
        return False
    parts = urlsplit(link)
    return parts.scheme == 'https' and parts.netloc in PROVIDER_HOSTS and parts.path.rstrip('/') == route[:-1]


def verify_account(opener, identity, scope, begin, end, deadline):
    account, mpan, serial, key = identity
    validate_private_alias(scope, identity)
    authorization = 'Basic ' + base64.b64encode((key + ':').encode('ascii')).decode('ascii')
    document = fetch_json(opener, f'/v1/accounts/{account}/', authorization, MAX_ACCOUNT_BYTES, deadline)
    verify_import_meter(document, account, mpan, serial, begin, end)
    return authorization


def capture_consumption(opener, identity, scope, begin, end, deadline):
    authorization = verify_account(opener, identity, scope, begin, end, deadline)
    _, mpan, serial, _ = identity

    route = f'/v1/electricity-meter-points/{mpan}/meters/{serial}/consumption/'
    expected = (end - begin) // HALF_HOUR
    pages = (expected + PAGE_SIZE - 1) // PAGE_SIZE
    rows = []
    for index in range(1, pages + 1):
        query = urlencode({
            'period_from': iso_utc(begin), 'period_to': iso_utc(end - ONE_SECOND),
            'page_size': PAGE_SIZE, 'page': index, 'order_by': 'period',
        })
        page = fetch_json(opener, route + '?' + query, authorization, MAX_PAGE_BYTES, deadline)
        if (not isinstance(page, dict) or set(page) != {'count', 'next', 'previous', 'results'} or
                type(page['count']) is not Number or not re.fullmatch(r'0|[1-9][0-9]{0,4}', page['count']) or
                not isinstance(page['results'], list)):
            raise CaptureError('invalid or incomplete Octopus consumption pagination')
        count = int(page['count'])
        partial = index == 1 and count < expected
        if ((not partial and count != expected) or len(page['results']) != min(PAGE_SIZE, count - len(rows)) or
                not matches_page_link(page['next'], route, index * PAGE_SIZE < count) or
                not matches_page_link(page['previous'], route, index > 1)):
            raise CaptureError('invalid or incomplete Octopus consumption pagination')
        append_readings(rows, page['results'], normalize_utc=True)
        if partial:
            raise CapturePending('Octopus readings are not complete for this cycle')
    limit = MAX_ANNUAL_BYTES if expected > MAX_ROWS else MAX_BYTES
    return encode_bundle(rows, scope, begin, end, byte_limit=limit)


def next_cycle_plan(seed, *, scheduled=False):
    miner = Path(__file__).resolve().parent.parent / 'bin' / ('canary-miner.exe' if os.name == 'nt' else 'canary-miner')
    try:
        installed = local_path(miner)
        info = installed.lstat()
        if not stat.S_ISREG(info.st_mode) or getattr(info, 'st_file_attributes', 0) & REPARSE_POINT:
            raise CaptureError('the installed miner beside the capture tools is unavailable')
        operand = f'.{os.sep}{seed.name}'
        flag = '--scheduled-readings-plan' if scheduled else '--next-readings-plan'
        creation = getattr(subprocess, 'CREATE_NO_WINDOW', 0) if scheduled else 0
        result = subprocess.run([installed, 'meter-bill', flag, operand],
                                cwd=seed.parent, stdin=subprocess.DEVNULL, capture_output=True, timeout=60,
                                creationflags=creation)
    except CaptureError:
        raise
    except (OSError, subprocess.TimeoutExpired, ValueError):
        raise CaptureError('could not run the installed billing planner') from None
    if result.returncode != 0 or result.stderr:
        raise CaptureError('no next readings plan is available from this workspace')
    try:
        if len(result.stdout) > 4096:
            raise ValueError('oversized plan')
        plan = decode_json(result.stdout)
        fields = {'schema_version', 'scope', 'begin_utc_ms', 'end_utc_ms', 'predecessor_sha256'}
        if scheduled:
            fields.update(('seed_sha256', 'ready'))
        if (not isinstance(plan, dict) or set(plan) != fields or
                type(plan['schema_version']) is not Number or plan['schema_version'] != '1' or
                type(plan['scope']) is not str or not valid_digest(plan['predecessor_sha256']) or
                (scheduled and (not valid_digest(plan['seed_sha256']) or type(plan['ready']) is not bool))):
            raise ValueError('invalid plan')
        boundaries = [plan['begin_utc_ms'], plan['end_utc_ms']]
        if any(type(value) is not Number or not re.fullmatch(r'[1-9][0-9]{0,14}', value)
               or int(value) % 1800000 for value in boundaries):
            raise ValueError('unaligned plan')
        begin, end = (EPOCH + int(value) * MILLISECOND for value in boundaries)
        result = (plan['scope'], begin, end, plan['predecessor_sha256'])
        return (*result, plan['seed_sha256'], plan['ready']) if scheduled else result
    except (ValueError, OverflowError, RecursionError):
        raise CaptureError('invalid readings plan from the installed miner') from None


def valid_digest(text):
    return type(text) is str and re.fullmatch(r'[a-f0-9]{64}', text) is not None


def windows_protect(payload, decrypt):
    import ctypes
    from ctypes import wintypes

    class Blob(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('data', ctypes.c_void_p)]

    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    transform = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    transform.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                          ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    transform.restype = wintypes.BOOL
    free = ctypes.WinDLL('kernel32').LocalFree
    free.argtypes = [ctypes.c_void_p]
    free.restype = ctypes.c_void_p
    raw = ctypes.create_string_buffer(payload, len(payload))
    source, result = Blob(len(raw), ctypes.addressof(raw)), Blob()
    try:
        if not transform(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(result)):
            raise CaptureError('Windows could not protect the current-user credential')
        if not 0 < result.size <= MAX_CREDENTIAL_BYTES or not result.data:
            raise CaptureError('Windows returned an invalid protected credential')
        return ctypes.string_at(result.data, result.size)
    finally:
        ctypes.memset(ctypes.addressof(raw), 0, len(raw))
        if result.data:
            if decrypt:
                ctypes.memset(result.data, 0, result.size)
            free(result.data)


def protect_credential(payload, *, decrypt=False):
    if os.name == 'nt':
        return windows_protect(payload, decrypt)
    cmd = ['/usr/bin/systemd-creds', '--user', '--name=canary-octopus']
    cmd += ['decrypt'] if decrypt else ['--with-key=host', 'encrypt']
    try:
        result = subprocess.run([*cmd, '-', '-'], input=payload, capture_output=True,
                                start_new_session=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        raise CaptureError('the systemd user credential service is unavailable') from None
    if result.returncode != 0 or not 0 < len(result.stdout) <= MAX_CREDENTIAL_BYTES:
        raise CaptureError('systemd could not protect the current-user credential')
    return result.stdout


def read_scheduled_identity(path, plan):
    info = path.lstat()
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
    if not stat.S_ISREG(info.st_mode) or getattr(info, 'st_file_attributes', 0) & REPARSE_POINT:
        raise CaptureError('scheduled credential must be a regular private file')
    if os.name != 'nt' and (info.st_uid != os.getuid() or info.st_mode & 0o077):
        raise CaptureError('scheduled credential must be private to the current user')
    with os.fdopen(os.open(path, flags), 'rb') as sealed:
        current = os.fstat(sealed.fileno())
        if not os.path.samestat(info, current) or not 0 < current.st_size <= MAX_CREDENTIAL_BYTES:
            raise CaptureError('scheduled credential moved or has an invalid size')
        payload = sealed.read(MAX_CREDENTIAL_BYTES + 1)
        if len(payload) != current.st_size:
            raise CaptureError('scheduled credential changed while reading')
    try:
        document = decode_json(protect_credential(payload, decrypt=True))
        if (not isinstance(document, dict) or set(document) != {'schema_version', 'seed_sha256', 'scope', 'identity'} or
                type(document['schema_version']) is not Number or document['schema_version'] != '1' or
                document['seed_sha256'] != plan[4] or document['scope'] != plan[0]):
            raise ValueError('credential binding')
        values = document['identity']
        if (type(values) is not list or len(values) != len(IDENTITY_PROMPTS) or
                any(type(value) is not str or not re.fullmatch(pattern, value)
                    for value, (_, pattern) in zip(values, IDENTITY_PROMPTS))):
            raise ValueError('credential identity')
        return tuple(values)
    except CaptureError:
        raise
    except (ValueError, RecursionError):
        raise CaptureError('scheduled credential does not match the verified workspace') from None


def validate_window(scope, begin, end, *, elapsed=True):
    if not re.fullmatch(r'[ -~]{1,128}', scope) or scope != scope.strip(' '):
        raise CaptureError('choose a valid private meter alias')
    if (begin >= end or (elapsed and end > dt.datetime.now(dt.timezone.utc)) or
            (end - begin) // HALF_HOUR > MAX_ANNUAL_ROWS):
        raise CaptureError('choose a valid cycle of up to 18,432 half hours')


def collect_window(output, scope, begin, end, *, seed=None, plan=None, verify_owner=None):
    scheduled = verify_owner is not None
    if scheduled:
        verify_owner()
    parent_state = output.parent.stat()
    if (output.name.casefold() in (BINDING_NAME, CREDENTIAL_NAME, CAPTURE_LEASE_NAME) or os.path.lexists(output) or
            not stat.S_ISDIR(parent_state.st_mode)):
        raise CaptureError('choose a new readings file in an existing local directory')
    identity = (read_scheduled_identity(output.parent / CREDENTIAL_NAME, plan) if scheduled else read_identity())
    binding = output.parent / BINDING_NAME
    already_bound = check_binding(binding, identity, scope)
    if scheduled and not already_bound:
        raise CaptureError('the verified meter binding for the scheduled credential is missing')

    def current_plan():
        return next_cycle_plan(seed, scheduled=True) if scheduled else next_cycle_plan(seed)

    if seed and current_plan() != plan:
        raise CaptureError('the readings plan changed before capture')
    if scheduled:
        verify_owner()
    bundle = capture_consumption(private_opener(), identity, scope, begin, end, time.monotonic() + 180)

    def verify_publication_plan():
        confirm_destination(output, parent_state)
        if current_plan() != plan:
            raise CaptureError('the readings plan changed during capture')
        if scheduled and not check_binding(binding, identity, scope):
            raise CaptureError('the verified meter binding for the scheduled credential is missing')
        if scheduled:
            verify_owner()
        confirm_destination(output, parent_state)

    confirm_destination(output, parent_state)
    if seed and not already_bound:
        verify_publication_plan()
    if not scheduled:
        pin_binding(binding, identity, scope)
        confirm_destination(output, parent_state)
    publish(output, bundle, before_publish=verify_publication_plan if seed else None)


def readings_path(workspace, end):
    return workspace / f'cycle-{(end - EPOCH) // MILLISECOND}.readings.json'


def enroll_scheduled(seed):
    path = seed.parent / CREDENTIAL_NAME
    binding = seed.parent / BINDING_NAME
    if os.path.lexists(path):
        raise CaptureError('an enrolled scheduled credential already exists')
    if not os.path.lexists(binding):
        raise CaptureError('verify this meter with an interactive capture before enrolling')
    parent_state = seed.parent.stat()
    plan = next_cycle_plan(seed, scheduled=True)
    scope, begin, end, _, seed_digest, _ = plan
    validate_window(scope, begin, end, elapsed=False)
    identity = read_identity()
    if not check_binding(binding, identity, scope):
        raise CaptureError('verify this meter with an interactive capture before enrolling')
    if next_cycle_plan(seed, scheduled=True) != plan:
        raise CaptureError('the readings plan changed before enrollment')
    verify_account(private_opener(), identity, scope, begin, end, time.monotonic() + 20)
    document = dict(schema_version=1, scope=scope, seed_sha256=seed_digest, identity=identity)
    sealed = protect_credential(json.dumps(document, ensure_ascii=True).encode('ascii'))

    def verify_enrollment():
        confirm_destination(path, parent_state)
        if next_cycle_plan(seed, scheduled=True) != plan or not check_binding(binding, identity, scope):
            raise CaptureError('the readings plan or meter binding changed during enrollment')
        confirm_destination(path, parent_state)

    confirm_destination(path, parent_state)
    publish(path, sealed, before_publish=verify_enrollment)
    print('Enrolled the verified Octopus meter for scheduled capture')


@contextmanager
def capture_lease(path):
    parent_state = path.parent.stat()
    flags = os.O_RDWR | os.O_CREAT | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_BINARY', 0)
    if os.path.lexists(path):
        original = path.lstat()
        if not stat.S_ISREG(original.st_mode) or getattr(original, 'st_file_attributes', 0) & REPARSE_POINT:
            raise CaptureError('scheduled capture lease must be a regular private file')
    with os.fdopen(os.open(path, flags, 0o600), 'r+b') as lease:
        lease_state = os.fstat(lease.fileno())

        def verify_owner():
            confirm_destination(path, parent_state)
            info = path.lstat()
            if (getattr(info, 'st_file_attributes', 0) & REPARSE_POINT or
                    not stat.S_ISREG(info.st_mode) or not os.path.samestat(info, lease_state)):
                raise CaptureError('scheduled capture lease moved')

        verify_owner()
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(lease.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lease.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise CaptureError('another scheduled capture owns this workspace') from None
        yield verify_owner


def run_scheduled(seed, seconds):
    credential = seed.parent / CREDENTIAL_NAME
    if not credential.is_file() or credential.is_symlink():
        raise CaptureError('enroll a scheduled Octopus credential first')

    def stop(_signum, _frame):
        raise KeyboardInterrupt

    previous_stop = signal.signal(signal.SIGTERM, stop)
    try:
        with capture_lease(seed.parent / CAPTURE_LEASE_NAME) as verify_owner:
            while True:
                verify_owner()
                plan = next_cycle_plan(seed, scheduled=True)
                scope, begin, end, _, _, ready = plan
                validate_window(scope, begin, end, elapsed=ready)
                if ready:
                    try:
                        collect_window(readings_path(seed.parent, end), scope, begin, end,
                                       seed=seed, plan=plan, verify_owner=verify_owner)
                    except CapturePending:
                        if seconds is None:
                            raise
                    else:
                        print('Saved a verified Octopus electricity import bundle', flush=True)
                if seconds is None:
                    return
                time.sleep(seconds)
    finally:
        signal.signal(signal.SIGTERM, previous_stop)


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--confirm-electricity-import', action='store_true', required=True,
                        help='authorize read-only access to the selected electricity import meter')
    parser.add_argument('--scope', help='local alias matching the tariff opening')
    parser.add_argument('--begin', type=utc, help='inclusive UTC half-hour boundary')
    parser.add_argument('--end', type=utc, help='exclusive UTC half-hour boundary')
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--next-cycle', type=Path, help='derive the next missing readings window from a recurring seed archive')
    action.add_argument('--enroll-scheduled', type=Path, help='verify and protect the current meter credentials for a recurring seed')
    action.add_argument('--run-scheduled', type=Path, help='capture the next missing cycle using the enrolled current-user credential')
    parser.add_argument('--watch-every-seconds', type=int, help='repeat scheduled capture every 60 to 86400 seconds')
    destination = parser.add_mutually_exclusive_group()
    destination.add_argument('--output', type=Path, help='new local readings file')
    destination.add_argument('--workspace', type=Path, help='existing recurring review directory')
    args = parser.parse_args()
    seed_argument = args.next_cycle or args.enroll_scheduled or args.run_scheduled
    if args.watch_every_seconds is not None and (not args.run_scheduled or not 60 <= args.watch_every_seconds <= 86400):
        parser.error('--watch-every-seconds requires scheduled capture and a 60 to 86400 second interval')
    if seed_argument:
        if any(value is not None for value in (args.scope, args.begin, args.end, args.output, args.workspace)):
            parser.error('a recurring seed supplies the alias, cycle and workspace')
    elif not (args.scope and args.begin and args.end and (args.output or args.workspace)):
        parser.error('provide --scope, --begin, --end and one destination or use --next-cycle')
    try:
        seed = local_path(seed_argument) if seed_argument else None
        plan = None
        if seed:
            if seed.suffix.lower() != '.cmr':
                raise CaptureError('choose a recurring seed review archive')
            if args.enroll_scheduled:
                enroll_scheduled(seed)
                return
            if args.run_scheduled:
                run_scheduled(seed, args.watch_every_seconds)
                return
            plan = next_cycle_plan(seed)
            args.scope, args.begin, args.end, _ = plan
            args.workspace = seed.parent
        try:
            validate_window(args.scope, args.begin, args.end)
        except CaptureError as error:
            parser.error(str(error))
        output = local_path(args.output or readings_path(args.workspace, args.end))
        collect_window(output, args.scope, args.begin, args.end, seed=seed, plan=plan)
    except CaptureError as error:
        parser.exit(1, f'could not capture Octopus consumption: {error}\n')
    except (OSError, ValueError, InvalidOperation, OverflowError, RecursionError):
        parser.exit(1, 'could not validate or publish the Octopus consumption bundle\n')
    except KeyboardInterrupt:
        parser.exit(130, 'Octopus consumption capture interrupted\n')
    print('Saved a verified Octopus electricity import bundle')


if __name__ == '__main__':
    main()
