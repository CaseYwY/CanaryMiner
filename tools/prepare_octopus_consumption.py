"""Prepare locally saved Octopus electricity import pages for meter-bill."""
import argparse
import datetime as dt
from decimal import Decimal, InvalidOperation
import json
import os
from pathlib import Path
import re
import stat
from urllib.parse import unquote, urlsplit

from capture_octopus_rates import EPOCH, HALF_HOUR, publish, utc


MAX_BYTES = 1024 * 1024
MAX_ROWS = 8192
MAX_PAGES = 96
MILLISECOND = dt.timedelta(milliseconds=1)
TIMESTAMP = re.compile(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(Z|[+-][0-9]{2}:[0-9]{2})')
METER_ROUTE = re.compile(r'/v1/electricity-meter-points/([0-9]{13})/meters/([A-Za-z0-9_.-]{1,128})/consumption/?')
REPARSE_POINT = getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0)


class Number(str):
    """A number read from JSON before any conversion or rounding."""


def unique_object(pairs):
    result = dict(pairs)
    if len(result) != len(pairs):
        raise ValueError('duplicate JSON field')
    return result


def bad_constant(_):
    raise ValueError('non-finite JSON number')


def timestamp(value):
    match = TIMESTAMP.fullmatch(value) if type(value) is str else None
    if not match:
        raise ValueError('expected timestamps with a UTC designation or numerical offset')
    suffix = match[1]
    if suffix != 'Z' and (int(suffix[1:3]) > 14 or int(suffix[4:6]) >= 60 or
                          (suffix[1:3] == '14' and suffix[4:6] != '00')):
        raise ValueError('invalid UTC offset')
    instant = dt.datetime.fromisoformat(value.replace('Z', '+00:00')).astimezone(dt.timezone.utc)
    if instant < EPOCH or (instant - EPOCH) % HALF_HOUR:
        raise ValueError('timestamp is outside an exact UTC half-hour boundary')
    return instant


def quantity(token):
    if type(token) is not Number or len(token) > 151:
        raise ValueError('invalid electricity import quantity')
    value = Decimal(token)
    if value < 0:
        raise ValueError('invalid electricity import quantity')
    if value.is_zero():
        return '0'
    _, digits, scale = value.as_tuple()
    digits = list(digits)
    while scale < 0 and digits[-1] == 0:
        digits.pop()
        scale += 1
    if scale < -3 or len(digits) + max(0, scale) > 38:
        raise ValueError('electricity import exceeds the quantity or precision limit')
    return format(Decimal((0, digits, scale)), 'f')


def local_path(path):
    absolute = Path(os.path.abspath(path))
    if os.name == 'nt':
        from ctypes import windll

        if len(absolute.drive) != 2 or absolute.drive[1] != ':' or ':' in str(absolute)[2:]:
            raise ValueError('use files on a local drive')
        if windll.kernel32.GetDriveTypeW(absolute.anchor) not in (2, 3, 6):
            raise ValueError('use files on a local drive')
        for parent in reversed(absolute.parents):
            info = parent.lstat()
            if not stat.S_ISDIR(info.st_mode) or info.st_file_attributes & REPARSE_POINT:
                raise ValueError('use a local directory without redirects')
    return absolute


def read_page(path):
    path = local_path(path)
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOFOLLOW', 0)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or getattr(info, 'st_file_attributes', 0) & REPARSE_POINT:
        raise ValueError('input must be a regular local file')
    with os.fdopen(os.open(path, flags), 'rb') as file:
        info = os.fstat(file.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= MAX_BYTES:
            raise ValueError('input page unavailable or oversized')
        data = file.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError('input page unavailable or oversized')
    return json.loads(data.decode('utf-8-sig'), parse_int=Number, parse_float=Number,
                      parse_constant=bad_constant, object_pairs_hook=unique_object)


def meter_from_link(value):
    if type(value) is not str or not 1 <= len(value) <= 4096:
        raise ValueError('invalid pagination link')
    parsed = urlsplit(value)
    match = METER_ROUTE.fullmatch(unquote(parsed.path, errors='strict'))
    if not match:
        raise ValueError('pagination link does not identify an electricity meter')
    # Use only the meter route for consistency checking. The address is never requested or retained.
    return match.groups()


def build_bundle(paths, scope, begin, end):
    rows = []
    meter = None
    expected = (end - begin) // HALF_HOUR
    if not 1 <= expected <= MAX_ROWS or not 1 <= len(paths) <= MAX_PAGES:
        raise ValueError('choose one to 8,192 half hours and at most 96 pages')
    for index, path in enumerate(paths):
        try:
            page = read_page(path)
            if not isinstance(page, dict) or set(page) != {'count', 'next', 'previous', 'results'}:
                raise ValueError('unexpected page fields')
            if type(page['count']) is not Number or page['count'] != str(expected):
                raise ValueError('page count does not match the requested window')
            for key, present in (('next', index + 1 < len(paths)), ('previous', index > 0)):
                value = page[key]
                if present:
                    route = meter_from_link(value)
                    if meter is not None and route != meter:
                        raise ValueError('pagination links identify different meters')
                    meter = route
                elif value is not None:
                    raise ValueError('pagination is incomplete or out of order')
            results = page['results']
            if not isinstance(results, list) or not 1 <= len(results) <= expected - len(rows):
                raise ValueError('page length does not match the requested window')
            for row in results:
                if not isinstance(row, dict) or set(row) != {'consumption', 'interval_start', 'interval_end'}:
                    raise ValueError('unexpected reading fields')
                start, finish = timestamp(row['interval_start']), timestamp(row['interval_end'])
                if finish - start != HALF_HOUR:
                    raise ValueError('expected half-hour readings')
                rows.append((start, finish, row['interval_start'], row['interval_end'], quantity(row['consumption'])))
        except (OSError, ValueError, OverflowError, InvalidOperation, RecursionError):
            raise ValueError(f'invalid or incomplete Octopus page {index + 1}') from None
    rows.sort(key=lambda row: row[0])
    cursor = begin
    for start, finish, *_ in rows:
        if start != cursor:
            raise ValueError('meter readings have a gap, overlap or unexpected boundary')
        cursor = finish
    if cursor != end:
        raise ValueError('meter readings have a gap, overlap or unexpected boundary')

    observed = dt.datetime.now(dt.timezone.utc)
    if end > observed:
        raise ValueError('choose a wholly elapsed meter window')
    metadata = json.dumps({
        'schema_version': 3, 'scope': scope,
        'observed_utc_ms': (observed - EPOCH) // MILLISECOND, 'end_utc_ms': (end - EPOCH) // MILLISECOND,
    }, ensure_ascii=False)
    # Keep the whitelisted timestamp and quantity fields. Pagination links may contain meter identifiers.
    records = ',\n'.join(
        f'{{"interval_start":{json.dumps(start)},"interval_end":{json.dumps(finish)},"consumption":{kwh}}}'
        for _, _, start, finish, kwh in rows
    )
    result = (metadata[:-1] + ',\n"consumption":[' + records + ']\n}\n').encode('utf-8')
    if len(result) > MAX_BYTES:
        raise ValueError('consumption bundle exceeds 1 MiB')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--confirm-electricity-import', required=True, action='store_true',
                        help='confirm all pages came from one whole-meter electricity import export')
    parser.add_argument('--scope', required=True, help='local alias matching the tariff opening')
    parser.add_argument('--begin', type=utc, required=True, help='inclusive UTC boundary at the saved opening')
    parser.add_argument('--end', type=utc, required=True, help='exclusive UTC boundary for the review')
    parser.add_argument('--output', type=Path, required=True, help='new local file')
    parser.add_argument('pages', type=Path, nargs='+', help='local JSON pages in pagination order')
    args = parser.parse_args()
    if not re.fullmatch(r'[ -~]{1,128}', args.scope) or args.scope != args.scope.strip(' '):
        parser.error('choose a valid private meter alias')
    try:
        output = local_path(args.output)
        if os.path.lexists(output):
            parser.error('output already exists')
        publish(output, build_bundle(args.pages, args.scope, args.begin, args.end))
    except ValueError as error:
        parser.exit(1, f'could not prepare consumption bundle: {error}\n')
    except OSError:
        parser.exit(1, 'could not publish the new consumption bundle\n')
    except KeyboardInterrupt:
        parser.exit(130, 'consumption bundle preparation interrupted\n')


if __name__ == '__main__':
    main()
