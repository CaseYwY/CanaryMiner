"""Offline checks for public rate pagination and exclusive publication."""
import json
from argparse import ArgumentTypeError
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest import mock


CAPTURE = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/capture_octopus_rates.py'))


class CaptureTests(unittest.TestCase):
    def test_accepts_documented_dated_products(self):
        parse = CAPTURE['product_code']
        for code in ('AGILE-FLEX-22-11-25', 'AGILE-24-10-01', 'GO-VAR-22-10-14'):
            self.assertEqual(parse(code), code)
        for code in ('GO-VAR-23-02-29', 'INTELLI-VAR-22-10-14', 'GO-VAR-22-10-14/../'):
            with self.assertRaises(ArgumentTypeError):
                parse(code)

    def test_sequential_requests_keep_raw_price_tokens(self):
        capture_pages = CAPTURE['capture_pages']
        utc = CAPTURE['utc']
        first = {'count': 101, 'next': 'https://unrequested.invalid/', 'previous': None, 'results': [0] * 100}
        last = '{"count":101,"next":null,"previous":"elsewhere","results":[3.151e1]}'
        root = 'https://api.oegb-kraken.energy/v1/products/example/electricity-tariffs/example'
        begin, end = utc('2023-03-01T00:00:00Z'), utc('2023-03-02T00:00:00Z')
        with mock.patch.dict(capture_pages.__globals__, {'fetch_page': mock.Mock(side_effect=[json.dumps(first), last])}):
            page_fetch = capture_pages.__globals__['fetch_page']
            bundle = capture_pages(None, root, 'standard-unit-rates', begin, end, 150)
        self.assertIn(last, bundle)
        urls = [call.args[1] for call in page_fetch.call_args_list]
        self.assertEqual(len(urls), 2)
        for page, url in enumerate(urls, 1):
            self.assertTrue(url.startswith(root + '/standard-unit-rates/?'), url)
            self.assertTrue(url.endswith(f'&page_size=100&page={page}'), url)

    def test_failed_sync_cleans_staging_and_publication_preserves_existing_file(self):
        publish = CAPTURE['publish']
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'capture.json'
            with mock.patch('os.fsync', side_effect=OSError('fixture disk error')):
                with self.assertRaises(OSError):
                    publish(output, b'fixture')
            self.assertEqual(list(Path(directory).iterdir()), [])
            publish(output, b'fixture')
            with self.assertRaises(FileExistsError):
                publish(output, b'replacement')
            self.assertEqual(output.read_bytes(), b'fixture')
            self.assertEqual(list(Path(directory).iterdir()), [output])


if __name__ == '__main__':
    unittest.main()
