"""Capture one verified Octopus import meter window for a local CanaryMiner review."""
import argparse
import base64
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
import ssl
import stat
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
    MAX_BYTES, MAX_ROWS, MILLISECOND, Number, append_readings, decode_json, encode_bundle, local_path, read_page,
)


# The credential destination is independent of the public price client's URL.
ORIGIN = 'https://api.oegb-kraken.energy'
MAX_ANNUAL_ROWS = 18432
MAX_ANNUAL_BYTES = 1792 * 1024
MAX_ACCOUNT_BYTES = 1024 * 1024
PROVIDER_HOSTS = ('api.oegb-kraken.energy', 'api.octopus.energy')
BINDING_NAME = '.canary-octopus-meter.json'


class CaptureError(ValueError):
    """A diagnostic safe to display without printing credentials or account data."""


def read_identity():
    if not sys.stdin.isatty():
        raise CaptureError('open an interactive terminal for masked account entry')
    patterns = (
        ('Octopus account number: ', r'A-[A-Za-z0-9]{1,32}'),
        ('Import MPAN: ', r'[0-9]{13}'),
        ('Meter serial: ', r'(?!\.{1,2}\Z)[A-Za-z0-9_.-]{1,128}'),
        ('Octopus API key: ', r'[!-9;-~]{1,256}'),  # Basic Auth username excludes colon.
    )
    values = []
    with warnings.catch_warnings():
        warnings.simplefilter('error', getpass.GetPassWarning)
        try:
            for prompt, pattern in patterns:
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


def capture_consumption(opener, identity, scope, begin, end, deadline):
    account, mpan, serial, key = identity
    validate_private_alias(scope, identity)
    authorization = 'Basic ' + base64.b64encode((key + ':').encode('ascii')).decode('ascii')
    document = fetch_json(opener, f'/v1/accounts/{account}/', authorization, MAX_ACCOUNT_BYTES, deadline)
    verify_import_meter(document, account, mpan, serial, begin, end)

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
                type(page['count']) is not Number or page['count'] != str(expected) or
                not isinstance(page['results'], list) or len(page['results']) != min(PAGE_SIZE, expected - len(rows)) or
                not matches_page_link(page['next'], route, index < pages) or
                not matches_page_link(page['previous'], route, index > 1)):
            raise CaptureError('invalid or incomplete Octopus consumption pagination')
        append_readings(rows, page['results'], normalize_utc=True)
    limit = MAX_ANNUAL_BYTES if expected > MAX_ROWS else MAX_BYTES
    return encode_bundle(rows, scope, begin, end, byte_limit=limit)


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--confirm-electricity-import', action='store_true', required=True,
                        help='authorize read-only access to the selected electricity import meter')
    parser.add_argument('--scope', required=True, help='local alias matching the tariff opening')
    parser.add_argument('--begin', type=utc, required=True, help='inclusive UTC half-hour boundary')
    parser.add_argument('--end', type=utc, required=True, help='exclusive UTC half-hour boundary')
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path, help='new local readings file')
    destination.add_argument('--workspace', type=Path, help='existing recurring review directory')
    args = parser.parse_args()
    if not re.fullmatch(r'[ -~]{1,128}', args.scope) or args.scope != args.scope.strip(' '):
        parser.error('choose a valid private meter alias')
    if (not args.begin < args.end <= dt.datetime.now(dt.timezone.utc) or
            (args.end - args.begin) // HALF_HOUR > MAX_ANNUAL_ROWS):
        parser.error('choose an elapsed window of up to 18,432 half hours')
    try:
        closing_ms = (args.end - EPOCH) // MILLISECOND
        output = local_path(args.output or args.workspace / f'cycle-{closing_ms}.readings.json')
        parent_state = output.parent.stat()
        if (output.name.casefold() == BINDING_NAME or os.path.lexists(output) or
                not stat.S_ISDIR(parent_state.st_mode)):
            raise CaptureError('choose a new readings file in an existing local directory')
        identity = read_identity()
        binding = output.parent / BINDING_NAME
        check_binding(binding, identity, args.scope)
        bundle = capture_consumption(private_opener(), identity, args.scope, args.begin, args.end, time.monotonic() + 180)
        confirm_destination(output, parent_state)
        pin_binding(binding, identity, args.scope)
        confirm_destination(output, parent_state)
        publish(output, bundle)
    except CaptureError as error:
        parser.exit(1, f'could not capture Octopus consumption: {error}\n')
    except (OSError, ValueError, InvalidOperation, OverflowError, RecursionError):
        parser.exit(1, 'could not validate or publish the Octopus consumption bundle\n')
    except KeyboardInterrupt:
        parser.exit(130, 'Octopus consumption capture interrupted\n')
    print('Saved a verified Octopus electricity import bundle')


if __name__ == '__main__':
    main()
