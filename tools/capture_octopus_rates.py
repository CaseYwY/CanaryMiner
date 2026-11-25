"""Capture public Octopus Agile prices for a local CanaryMiner meter review."""
import argparse
import datetime as dt
from http.client import HTTPException
import json
import os
from pathlib import Path
import re
import tempfile
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import HTTPRedirectHandler, Request, build_opener


ORIGIN = 'https://api.oegb-kraken.energy'
REGIONS = 'ABCDEFGHJKLMNP'
PAYMENTS = ('DIRECT_DEBIT', 'NON_DIRECT_DEBIT')
PAGE_SIZE = 100
MAX_PAGE_BYTES = 64 * 1024
MAX_FILE_BYTES = 2 * 1024 * 1024
MAX_UNIT_ROWS = 8192
MAX_STANDING_ROWS = 256
HALF_HOUR = dt.timedelta(minutes=30)
ONE_SECOND = dt.timedelta(seconds=1)
EPOCH = dt.datetime(1970, 1, 1, tzinfo=dt.timezone.utc)


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def iso_utc(instant):
    return instant.isoformat(timespec='seconds').replace('+00:00', 'Z')


def utc(value):
    try:
        if not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z', value):
            raise ValueError
        instant = dt.datetime.fromisoformat(value[:-1]).replace(tzinfo=dt.timezone.utc)
        if instant < EPOCH or (instant - EPOCH) % HALF_HOUR:
            raise ValueError
        return instant
    except ValueError:
        raise argparse.ArgumentTypeError('expected a UTC half-hour boundary, YYYY-MM-DDTHH:MM:SSZ') from None


def product_code(value):
    match = re.fullmatch(r'AGILE-(?:FLEX-)?([0-9]{2}-[0-9]{2}-[0-9]{2})', value)
    if match:
        try:
            dt.date.fromisoformat('20' + match[1])
            return value
        except ValueError:
            pass
    raise argparse.ArgumentTypeError('expected a dated AGILE or AGILE-FLEX import product code')


def fetch_page(opener, url):
    request = Request(url, headers={'Accept': 'application/json'})
    with opener.open(request, timeout=15) as response:
        if response.status != 200 or response.url != url:
            raise ValueError('unexpected response from the public rate endpoint')
        raw = response.read(MAX_PAGE_BYTES + 1)
    if len(raw) > MAX_PAGE_BYTES:
        raise ValueError('public rate page exceeds the size limit')
    return raw.decode('utf-8')


def capture_pages(opener, tariff_url, endpoint, begin, end, row_limit):
    pages = []
    total = None
    received = 0
    while total is None or received < total:
        page = len(pages) + 1
        query = urlencode({
            'period_from': iso_utc(begin), 'period_to': iso_utc(end),
            'page_size': PAGE_SIZE, 'page': page,
        })
        raw = fetch_page(opener, f'{tariff_url}/{endpoint}/?{query}')
        data = json.loads(raw)
        if not isinstance(data, dict) or set(data) != {'count', 'next', 'previous', 'results'}:
            raise ValueError(f'invalid {endpoint} page')
        count = data['count']
        rows = data['results']
        if (type(count) is not int or not 1 <= count <= row_limit or
                (total is not None and total != count) or not isinstance(rows, list) or
                len(rows) != min(PAGE_SIZE, count - received)):
            raise ValueError(f'invalid or incomplete {endpoint} page')
        total = count
        received += len(rows)
        for field, expected in [('next', received < count), ('previous', page > 1)]:
            link = data[field]
            valid = isinstance(link, str) and 1 <= len(link) <= 2048 if expected else link is None
            if not valid:
                raise ValueError(f'invalid {endpoint} pagination')
        # The archive keeps the original JSON numeric text. Pagination URLs are never requested.
        pages.append(raw)
    return ',\n'.join(pages)


def publish(output, bundle):
    pending = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.canary-octopus-', delete=False) as staging:
            pending = Path(staging.name)
            staging.write(bundle)
            staging.flush()
            os.fsync(staging.fileno())
        if os.name == 'nt':
            pending.rename(output)
        else:
            os.link(pending, output)
    finally:
        if pending is not None:
            pending.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--product', type=product_code, required=True)
    parser.add_argument('--region', choices=tuple(REGIONS), required=True)
    parser.add_argument('--payment-method', choices=PAYMENTS, required=True)
    parser.add_argument('--begin', type=utc, required=True, help='inclusive UTC boundary')
    parser.add_argument('--end', type=utc, required=True, help='exclusive UTC boundary')
    parser.add_argument('--output', type=Path, required=True, help='new local file')
    args = parser.parse_args()
    if not args.begin < args.end <= dt.datetime.now(dt.timezone.utc):
        parser.error('choose a wholly elapsed rate window')
    if (args.end - args.begin) // HALF_HOUR > MAX_UNIT_ROWS:
        parser.error('rate window exceeds 8,192 half hours')
    if os.path.lexists(args.output):
        parser.error('output already exists')

    tariff = f'E-1R-{args.product}-{args.region}'
    tariff_url = f'{ORIGIN}/v1/products/{args.product}/electricity-tariffs/{tariff}'
    opener = build_opener(NoRedirect())
    try:
        inclusive_end = args.end - ONE_SECOND
        unit = capture_pages(opener, tariff_url, 'standard-unit-rates', args.begin, inclusive_end, MAX_UNIT_ROWS)
        fees = capture_pages(opener, tariff_url, 'standing-charges', args.begin, inclusive_end, MAX_STANDING_ROWS)
        observed = (dt.datetime.now(dt.timezone.utc) - EPOCH) // dt.timedelta(milliseconds=1)
        meta = json.dumps({
            'schema_version': 1, 'product_code': args.product, 'tariff_code': tariff,
            'payment_method': args.payment_method, 'observed_utc_ms': observed,
        })
        bundle = f'{meta[:-1]},\n"unit_rate_pages": [{unit}],\n"standing_charge_pages": [{fees}]\n}}\n'.encode('utf-8')
        if len(bundle) > MAX_FILE_BYTES:
            raise ValueError('public rate capture exceeds the size limit')
        publish(args.output, bundle)
    except (HTTPException, URLError) as error:
        parser.exit(1, f'could not read the public rate endpoint ({type(error).__name__})\n')
    except KeyboardInterrupt:
        parser.exit(130, 'rate capture interrupted\n')
    except (OSError, ValueError) as error:
        parser.exit(1, f'could not save the rate capture: {error}\n')


if __name__ == '__main__':
    main()
