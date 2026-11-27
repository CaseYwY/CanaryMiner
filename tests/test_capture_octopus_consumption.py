"""Exercise authenticated request construction, validation and atomic private publication."""
import base64
from contextlib import redirect_stderr, redirect_stdout
import copy
import datetime as dt
from email.message import Message
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock
from urllib.error import HTTPError
from urllib.parse import parse_qs, urlsplit
import warnings

with mock.patch.object(sys, 'path', [*sys.path, str(Path(__file__).resolve().parents[1] / 'tools')]):
    import capture_octopus_consumption as capture


ACCOUNT, POINT, SERIAL, KEY = 'A-SYNTH22', '0000000000000', 'example-main-meter', 'fixture-octopus-key'
IDENTITY = ACCOUNT, POINT, SERIAL, KEY
SCOPE = 'local-billing-alias'
ROUTE = f'/v1/electricity-meter-points/{POINT}/meters/{SERIAL}/consumption/'
PROVIDER = 'https://api.oegb-kraken.energy'
BEGIN = capture.utc('2023-03-01T00:00:00Z')
END = capture.utc('2023-03-31T23:00:00Z')


def account():
    return {'number': ACCOUNT, 'properties': [
        {'moved_in_at': '2023-02-01T00:00:00Z', 'moved_out_at': None,
         'address_line_1': 'fixture-private-property',
         'electricity_meter_points': [{'mpan': POINT, 'is_export': False,
                                       'meters': [{'serial_number': SERIAL}]}]}]}


def page_documents(begin, end, *, results=None):
    count = (end - begin) // capture.HALF_HOUR
    if results is None:
        def stamp(index):
            instant = begin + index * capture.HALF_HOUR
            bst = dt.timezone(dt.timedelta(hours=1))
            if instant >= capture.utc('2023-03-26T01:00:00Z'):
                return instant.astimezone(bst).isoformat()
            return capture.iso_utc(instant)
        results = [dict(consumption=0.015, interval_start=stamp(i), interval_end=stamp(i + 1)) for i in range(count)]
    pages = []
    for index in range(0, count, capture.PAGE_SIZE):
        link = PROVIDER + ROUTE
        pages.append(dict(count=count, next=link if index + capture.PAGE_SIZE < count else None,
                          previous=link if index else None, results=results[index:index + capture.PAGE_SIZE]))
    return pages


class Response(io.BytesIO):
    status = 200

    def __init__(self, url, body, headers=None):
        super().__init__(body)
        self.url = url
        self.headers = Message()
        for key, value in (headers if headers is not None else
                           [('Content-Type', 'application/json'), ('Content-Length', str(len(body)))]):
            self.headers[key] = value

    def geturl(self):
        return self.url


class Provider:
    def __init__(self, *documents):
        self.documents = iter(documents)
        self.requests = []

    def open(self, request, *, timeout):
        self.requests.append(request)
        assert 0 < timeout <= 15
        data = next(self.documents)
        if isinstance(data, Exception):
            raise data
        body = data if isinstance(data, bytes) else json.dumps(data).encode('ascii')
        return Response(request.full_url, body)


class AuthenticatedCaptureTests(unittest.TestCase):
    def capture(self, provider, scope=SCOPE):
        return capture.capture_consumption(provider, IDENTITY, scope, BEGIN, END, time.monotonic() + 10)

    def command(self, provider, destination, *extra, workspace=False, identity=IDENTITY, scope=SCOPE):
        stdout, stderr = io.StringIO(), io.StringIO()
        args = ['capture_octopus_consumption.py', '--begin', capture.iso_utc(BEGIN), '--end', capture.iso_utc(END),
                '--scope', scope, '--workspace' if workspace else '--output', str(destination), *extra]
        with (redirect_stderr(stderr), redirect_stdout(stdout), mock.patch.object(sys, 'argv', args),
              mock.patch.object(capture, 'read_identity', return_value=identity) as prompt,
              mock.patch.object(capture, 'private_opener', return_value=provider)):
            try:
                capture.main()
                code = 0
            except SystemExit as e:
                code = e.code
        for secret in (*IDENTITY, 'fixture-private-property'):
            self.assertNotIn(secret, stdout.getvalue() + stderr.getvalue())
        return code, prompt.call_count, stderr.getvalue()

    def test_capture_fixes_endpoints_and_reads_full_spring_month(self):
        provider = Provider(account(), *page_documents(BEGIN, END))
        blob = self.capture(provider)
        report = json.loads(blob)
        self.assertEqual(report['scope'], SCOPE)
        self.assertEqual(len(report['consumption']), 1486)
        self.assertEqual(report['consumption'][1200]['consumption'], 0.015)
        self.assertEqual(report['consumption'][-1]['interval_end'], '2023-03-31T23:00:00Z')
        self.assertIn(b'"interval_end":"2023-03-26T01:00:00Z"', blob)
        for secret in (*IDENTITY, 'fixture-private-property', 'https://'):
            self.assertNotIn(secret.encode('ascii'), blob)
        self.assertEqual(len(provider.requests), 16)
        self.assertEqual(provider.requests[0].full_url, PROVIDER + f'/v1/accounts/{ACCOUNT}/')
        auth = 'Basic ' + base64.b64encode((KEY + ':').encode('ascii')).decode('ascii')
        for index, request in enumerate(provider.requests):
            self.assertEqual(request.get_method(), 'GET')
            self.assertEqual(request.unredirected_hdrs['Authorization'], auth)
            if index:
                url = urlsplit(request.full_url)
                self.assertEqual(url.path, ROUTE)
                self.assertEqual(parse_qs(url.query), dict(page=[str(index)], page_size=['100'], order_by=['period'],
                                                           period_from=['2023-03-01T00:00:00Z'],
                                                           period_to=['2023-03-31T22:59:59Z']))

    def test_export_moved_out_wrong_or_ambiguous_meters_are_rejected_before_reads(self):
        for edit in ('export', 'wrong-account', 'wrong-serial', 'wrong-window', 'ambiguous'):
            sample = account()
            found = sample['properties'][0]
            point = found['electricity_meter_points'][0]
            if edit == 'export':
                point['is_export'] = True
            elif edit == 'wrong-account':
                sample['number'] = 'A-OTHER'
            elif edit == 'wrong-serial':
                point['meters'][0]['serial_number'] = 'unselected-meter'
            elif edit == 'wrong-window':
                found['moved_out_at'] = '2023-03-20T00:00:00Z'
            elif edit == 'ambiguous':
                found['electricity_meter_points'].append(copy.deepcopy(point))
            provider = Provider(sample)
            with self.subTest(edit=edit), self.assertRaises(capture.CaptureError):
                self.capture(provider)
            self.assertEqual(len(provider.requests), 1)

    def test_annual_capture_fits_the_current_native_archive_limit(self):
        end = capture.utc('2024-03-01T00:00:00Z')
        provider = Provider(account(), *page_documents(BEGIN, end))
        bundle = capture.capture_consumption(provider, IDENTITY, SCOPE, BEGIN, end, time.monotonic() + 15)
        report = json.loads(bundle)
        self.assertEqual(len(report['consumption']), 366 * 48)
        self.assertLess(len(bundle), 1792 * 1024)
        self.assertEqual(len(provider.requests), 177)

    def test_alias_must_not_embed_account_credentials_or_meter_identifiers(self):
        for value in IDENTITY:
            provider = Provider()
            with self.subTest(identifier=value), self.assertRaises(capture.CaptureError):
                self.capture(provider, f'home-{value.lower()}-import')
            self.assertEqual(provider.requests, [])
        capture.validate_private_alias('word-alias', (ACCOUNT, POINT, 'a', KEY))
        with self.assertRaises(capture.CaptureError):
            capture.validate_private_alias('meter-a', (ACCOUNT, POINT, 'a', KEY))

    def test_workspace_pins_meter_and_alias_but_allows_key_rotation(self):
        consent = '--confirm-electricity-import'
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            target = workspace / 'initial.json'
            self.assertEqual(self.command(Provider(account(), *page_documents(BEGIN, END)), target, consent)[:2], (0, 1))
            binding = workspace / capture.BINDING_NAME
            saved = binding.read_bytes()
            for value in (*IDENTITY, SCOPE):
                self.assertNotIn(value.encode('ascii'), saved)
            self.assertEqual(set(json.loads(saved)), {'schema_version', 'salt', 'digest'})
            next_file = workspace / 'next.json'
            for changed, alias in (((ACCOUNT, POINT, 'other-meter', KEY), SCOPE), (IDENTITY, 'different-alias')):
                provider = Provider()
                code, _, error = self.command(provider, next_file, consent, identity=changed, scope=alias)
                self.assertEqual(code, 1)
                self.assertIn('differs from the local workspace binding', error)
                self.assertEqual(provider.requests, [])
                self.assertFalse(next_file.exists())
            rotated = (ACCOUNT, POINT, SERIAL, 'fixture-replacement-key')
            self.assertEqual(self.command(Provider(account(), *page_documents(BEGIN, END)), next_file, consent,
                                          identity=rotated)[:2], (0, 1))
            self.assertEqual(binding.read_bytes(), saved)
            self.assertTrue(next_file.is_file())

    def test_workspace_replacement_during_capture_refuses_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original, moved = root / 'workspace', root / 'moved'
            original.mkdir()
            provider = Provider(account(), *page_documents(BEGIN, END))
            open_page = provider.open

            def move_on_last_page(request, *, timeout):
                response = open_page(request, timeout=timeout)
                if len(provider.requests) == 16:
                    original.rename(moved)
                    original.mkdir()
                return response

            with mock.patch.object(provider, 'open', side_effect=move_on_last_page):
                code, _, error = self.command(provider, original, '--confirm-electricity-import', workspace=True)
            self.assertEqual(code, 1)
            self.assertIn('destination workspace moved', error)
            self.assertEqual(list(original.iterdir()), [])
            self.assertEqual(list(moved.iterdir()), [])

    def test_incomplete_or_wrong_meter_never_replaces_a_workspace_file(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            target = workspace / 'cycle-1680303600000.readings.json'
            consent = '--confirm-electricity-import'
            provider = Provider(account(), *page_documents(BEGIN, END))
            self.assertEqual(self.command(provider, workspace, workspace=True)[:2], (2, 0))
            self.assertEqual(provider.requests, [])
            for defect in ('missing-reading', 'duplicate-interval', 'wrong-host', 'wrong-route', 'exponent'):
                broken = page_documents(BEGIN, END)
                if defect == 'missing-reading':
                    broken[-1]['results'].pop()
                elif defect == 'duplicate-interval':
                    broken[-1]['results'][-1] = broken[-1]['results'][-2]
                elif defect == 'wrong-host':
                    broken[0]['next'] = 'https://unrequested.invalid' + ROUTE
                elif defect == 'wrong-route':
                    broken[0]['next'] = broken[0]['next'].replace(SERIAL, 'different')
                elif defect == 'exponent':
                    broken[-1] = json.dumps(broken[-1]).replace('0.015', '1e9999').encode('ascii')
                with self.subTest(defect=defect):
                    self.assertEqual(self.command(Provider(account(), *broken), workspace, consent, workspace=True)[:2],
                                     (1, 1))
                    self.assertEqual(list(workspace.iterdir()), [])
            self.assertEqual(self.command(provider, workspace, consent, workspace=True)[:2], (0, 1))
            saved = target.read_bytes()
            self.assertEqual(self.command(Provider(), workspace, consent, workspace=True)[:2], (1, 0))
            self.assertEqual(target.read_bytes(), saved)

    def test_rejects_auth_and_transport_failures_without_provider_details(self):
        for status in (302, 401, 403, 429, 503):
            url = PROVIDER + f'/v1/accounts/{ACCOUNT}/'
            payload = f'supplier details including {KEY}'.encode()
            provider = Provider(HTTPError(url, status, f'private {SERIAL}', {}, io.BytesIO(payload)))
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'readings.json'
                code, prompts, error = self.command(provider, output, '--confirm-electricity-import')
                self.assertEqual((code, prompts), (1, 1))
                self.assertIn('could not capture Octopus consumption', error)
                self.assertFalse(output.exists())
        for body, headers in ((b'{}', [('Content-Type', 'application/json'), ('Content-Length', '70')]),
                              (b'{}', [('Content-Type', 'application/json'), ('Content-Encoding', 'gzip')]),
                              (b' ' * 99, [('Content-Type', 'application/json')]),
                              (b'{"count":1,"count":1}', [('Content-Type', 'application/json')])):
            opener = mock.Mock()
            opener.open.side_effect = lambda request, timeout: Response(request.full_url, body, headers)
            with self.assertRaises(capture.CaptureError):
                capture.fetch_json(opener, '/v1/fixture/', 'fixture', 32, time.monotonic() + 10)

    def test_masked_entry_blocks_redirected_input_and_fallback(self):
        with mock.patch.object(sys.stdin, 'isatty', return_value=False), mock.patch.object(capture.getpass, 'getpass') as ask:
            with self.assertRaises(capture.CaptureError):
                capture.read_identity()
            ask.assert_not_called()

        def fallback(*args):
            warnings.warn('fixture', capture.getpass.GetPassWarning)
            self.fail('fallback attempted to echo a credential')

        with (mock.patch.object(sys.stdin, 'isatty', return_value=True),
              mock.patch.object(capture.getpass, 'getpass', side_effect=fallback)):
            with self.assertRaises(capture.CaptureError):
                capture.read_identity()
        with (mock.patch.object(sys.stdin, 'isatty', return_value=True),
              mock.patch.object(capture.getpass, 'getpass', side_effect=IDENTITY)):
            self.assertEqual(capture.read_identity(), IDENTITY)

    def test_default_opener_ignores_proxies_and_tls_key_logging(self):
        with tempfile.TemporaryDirectory() as directory:
            logfile = Path(directory) / 'keys.log'
            with mock.patch.dict(os.environ, {'HTTPS_PROXY': 'http://proxy.invalid:1', 'SSLKEYLOGFILE': str(logfile)}):
                opener = capture.private_opener()
            handlers = opener.handlers
            https = next(handler for handler in handlers if isinstance(handler, capture.HTTPSHandler))
            self.assertTrue(https._context.check_hostname)
            self.assertEqual(https._context.verify_mode, capture.ssl.CERT_REQUIRED)
            self.assertIsNone(https._context.keylog_filename)
            proxy = next((h for h in handlers if isinstance(h, capture.ProxyHandler)), None)
            self.assertTrue(proxy is None or not proxy.proxies)
            self.assertFalse(logfile.exists())


if __name__ == '__main__':
    unittest.main()
