"""config-v1 / stats-v1 schema files: well-formed, the example conforms to config-v1, the C++ parser's accepted keys equal the
schema's properties, and the stats schema covers every member the C++ writer emits. A small structural validator is used
(no third-party jsonschema dependency)."""
import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(rel):
    with open(os.path.join(ROOT, rel), encoding='utf8') as f:
        return json.load(f)


def validate(value, schema, root, path='$'):
    """Returns a list of problems. Supports the subset the product schemas use."""
    problems = []
    if '$ref' in schema:
        ref = root
        for part in schema['$ref'].lstrip('#/').split('/'):
            ref = ref[part]
        return validate(value, ref, root, path)
    if 'const' in schema and value != schema['const']:
        problems.append('%s: expected %r' % (path, schema['const']))
    if 'enum' in schema and value not in schema['enum']:
        problems.append('%s: %r not in %r' % (path, value, schema['enum']))
    if 'oneOf' in schema:
        ok = sum(1 for s in schema['oneOf'] if not validate(value, s, root, path))
        if ok != 1:
            problems.append('%s: matches %d of oneOf' % (path, ok))
        return problems
    t = schema.get('type')
    types = t if isinstance(t, list) else ([t] if t else [])
    pytypes = {'object': dict, 'array': list, 'string': str, 'boolean': bool, 'null': type(None),
               'number': (int, float), 'integer': int}
    if types:
        good = False
        for x in types:
            if isinstance(value, bool) and x in ('number', 'integer'):
                continue
            if isinstance(value, pytypes[x]):
                good = True
        if not good:
            problems.append('%s: type %s expected, got %s' % (path, types, type(value).__name__))
            return problems
    if isinstance(value, str) and 'pattern' in schema and not re.search(schema['pattern'], value):
        problems.append('%s: does not match %s' % (path, schema['pattern']))
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if 'minimum' in schema and value < schema['minimum']:
            problems.append('%s: below minimum' % path)
        if 'maximum' in schema and value > schema['maximum']:
            problems.append('%s: above maximum' % path)
    if isinstance(value, list):
        if 'minItems' in schema and len(value) < schema['minItems']:
            problems.append('%s: too few items' % path)
        if 'maxItems' in schema and len(value) > schema['maxItems']:
            problems.append('%s: too many items' % path)
        if 'items' in schema:
            for i, item in enumerate(value):
                problems += validate(item, schema['items'], root, '%s[%d]' % (path, i))
    if isinstance(value, dict):
        props = schema.get('properties', {})
        for key in schema.get('required', []):
            if key not in value:
                problems.append('%s: missing required %s' % (path, key))
        ap = schema.get('additionalProperties', True)
        for key, item in value.items():
            if key in props:
                problems += validate(item, props[key], root, '%s.%s' % (path, key))
            elif ap is False:
                problems.append('%s: unknown key %s' % (path, key))
            elif isinstance(ap, dict):
                problems += validate(item, ap, root, '%s.%s' % (path, key))
    return problems


class ConfigSchemaTests(unittest.TestCase):
    def test_example_conforms_and_uses_placeholders(self):
        schema = load('config/config-v1.schema.json')
        example = load('config/config.example.json')
        self.assertEqual(validate(example, schema, schema), [])
        self.assertEqual(example['schema_version'], 1)
        self.assertIn('REPLACE', example['wallet'])
        self.assertIsNone(re.search(r'prl1[a-z0-9]{20,}', json.dumps(example)))

    def test_schema_rejects_what_the_parser_rejects(self):
        schema = load('config/config-v1.schema.json')
        bad = [
            {},                                                 # no schema_version
            {'schema_version': 2},                              # unsupported version
            {'schema_version': 1, 'walet': 'x'},                # unknown key
            {'schema_version': 1, 'api': {'bind': '0.0.0.0'}},  # not loopback
            {'schema_version': 1, 'api': {'port': 0}},
            {'schema_version': 1, 'pools': []},
            {'schema_version': 1, 'pools': [{'url': 'kryptex', 'network': 'testnet'}]},
            {'schema_version': 1, 'log': {'max_files': 0}},
            {'schema_version': 1, 'shutdown': {'drain_seconds': 601}},
            {'schema_version': 1, 'worker': 'bad worker'},
            {'schema_version': 1, 'devices': [99]},
        ]
        for doc in bad:
            with self.subTest(doc=doc):
                self.assertNotEqual(validate(doc, schema, schema), [])

    def test_every_config_key_is_documented_by_the_schema(self):
        # the C++ parser's accepted keys (src/product/config.h) must equal the schema's properties
        with open(os.path.join(ROOT, 'src', 'product', 'config.h'), encoding='utf8') as f:
            text = f.read()
        top = re.search(r'onlyKeys\(root, "the config", \{([^}]*)\}', text).group(1)
        schema = load('config/config-v1.schema.json')
        self.assertEqual(set(re.findall(r'"([a-z_]+)"', top)), set(schema['properties']))
        for section in ('api', 'log', 'shutdown'):
            m = re.search(r'onlyKeys\(\*?\w+, "%s", \{([^}]*)\}' % section, text)
            self.assertIsNotNone(m, section)
            self.assertEqual(set(re.findall(r'"([a-z_]+)"', m.group(1))), set(schema['properties'][section]['properties']), section)
        m = re.search(r'onlyKeys\(p, "a pool entry", \{([^}]*)\}', text)
        pool_obj = [s for s in schema['properties']['pools']['items']['oneOf'] if s.get('type') == 'object'][0]
        self.assertEqual(set(re.findall(r'"([a-z_]+)"', m.group(1))), set(pool_obj['properties']))


class RuntimePreferencesSchemaTests(unittest.TestCase):
    def test_defaults_boundaries_and_writer_keys(self):
        schema = load('config/runtime-preferences-v1.schema.json')
        self.assertEqual(validate({'schema_version': 1}, schema, schema), [])
        for number in (0, 1, 345, 1000):
            self.assertEqual(validate({'schema_version': 1, 'work_wait_ms': number, 'history_enabled': False}, schema, schema), [])
        for bad in ({}, {'schema_version': 2}, {'schema_version': 1, 'work_wait_ms': -1},
                    {'schema_version': 1, 'work_wait_ms': 1001}, {'schema_version': 1, 'work_wait_ms': True},
                    {'schema_version': 1, 'history_enabled': 1}, {'schema_version': 1, 'wallet': 'synthetic'}):
            self.assertTrue(validate(bad, schema, schema))
        with open(os.path.join(ROOT, 'src', 'product', 'runtime_preferences.h'), encoding='utf8') as stream:
            writer = stream.read().split('inline std::string runtimePreferencesJson', 1)[1].split('inline bool parseRuntimePreferences', 1)[0]
        self.assertEqual(set(re.findall(r'\\"([a-z_]+)\\":', writer)), set(schema['properties']))


class ManualMarketSchemaTests(unittest.TestCase):
    def test_disabled_example_shape_and_native_keys(self):
        schema = load('config/manual-market-preferences.schema.json')
        example = load('config/manual-market-preferences.example.json')
        self.assertEqual(validate(example, schema, schema), [])
        self.assertFalse(example['enabled'])
        self.assertFalse(example['fx_enabled'])
        self.assertEqual(example['prl_usdt'], '')
        self.assertEqual(example['usdt_usd'], '')
        self.assertEqual(example['prl_as_of_utc'], 0)
        self.assertEqual(example['fx_as_of_utc'], 0)
        self.assertEqual(example['confirmed_utc'], 0)
        for key, value in (('schema_version', 2), ('asset', 'synthetic-other-asset'), ('token_quote', 'USD'),
                           ('fx_base', 'USD'), ('fx_quote', 'USDT'), ('prl_usdt', 2.5), ('usdt_usd', '1e3'),
                           ('enabled', 1), ('validity_seconds', 0), ('max_fx_skew_seconds', 3601),
                           ('prl_as_of_utc', -1), ('wallet', 'synthetic')):
            with self.subTest(key=key):
                self.assertTrue(validate({**example, key: value}, schema, schema))
        with open(os.path.join(ROOT, 'src', 'product', 'manual_market_preferences.h'), encoding='utf8') as stream:
            native = stream.read()
        writer = native.split('inline std::string manualMarketJson', 1)[1].split('inline bool parseManualMarket', 1)[0]
        parser = re.search(r'const char \*keys\[\] = \{([^}]+)\}', native).group(1)
        self.assertEqual(set(re.findall(r'\\"([a-z_]+)\\":', writer)), set(schema['properties']))
        self.assertEqual(set(re.findall(r'"([a-z_]+)"', parser)), set(schema['properties']))
        self.assertEqual(set(schema['required']), set(schema['properties']))


class ElectricityPreferencesSchemaTests(unittest.TestCase):
    def test_disabled_defaults_boundaries_and_native_keys(self):
        schema = load('config/electricity-preferences.schema.json')
        example = load('config/electricity-preferences.example.json')
        self.assertEqual(validate(example, schema, schema), [])
        self.assertFalse(example['collect_energy'])
        self.assertFalse(example['tariff_enabled'])
        self.assertFalse(example['exclusions_acknowledged'])
        for key in ('country', 'region', 'supplier', 'product', 'version', 'currency', 'rate_per_kwh'):
            self.assertEqual(example[key], '')
        for key, value in (('schema_version', 2), ('rule_model', 'shared_progressive'), ('measurement_basis', 'whole_meter'),
                           ('collect_energy', 1), ('show_energy', 0), ('rate_per_kwh', 0.5), ('rate_per_kwh', '1e2'),
                           ('clock', 'named_iana'), ('record_interval_ms', 100), ('maximum_gap_ms', 60001),
                           ('country', 'bad\x1b[2J'), ('tax_rule', 'synthetic'), ('currency', 'USDT')):
            with self.subTest(key=key):
                self.assertTrue(validate({**example, key: value}, schema, schema))
        with open(os.path.join(ROOT, 'src', 'product', 'electricity_preferences.h'), encoding='utf8') as stream:
            native = stream.read()
        writer = native.split('inline std::string electricityPreferencesJson', 1)[1].split('inline bool parseElectricityPreferences', 1)[0]
        parser = re.search(r'const char \*keys\[\]\s*=\s*\{([^}]+)\}', native).group(1)
        self.assertEqual(set(re.findall(r'\\"([a-z_]+)\\":', writer)), set(schema['properties']))
        self.assertEqual(set(re.findall(r'"([a-z_]+)"', parser)), set(schema['properties']))
        self.assertEqual(set(schema['required']), set(schema['properties']))


class TariffDeclarationSchemaTests(unittest.TestCase):
    def test_example_and_strict_nested_contract(self):
        schema = load('config/tariff-declaration.schema.json')
        example = load('config/tariff-declaration.example.json')
        self.assertEqual(validate(example, schema, schema), [])
        self.assertIn('SYNTHETIC', example['source_reference'])
        self.assertEqual(example['opening']['origin'], 'declared_forecast')
        self.assertEqual(example['plan']['tiers'], 'progressive')
        self.assertEqual(example['plan']['revisions'][0]['time_mode'], 'calendar')
        for path, value in ((('schema_version',), 2), (('acknowledged',), False),
                            (('plan', 'currency'), 'USDT'), (('plan', 'cycle_charge'), 0.2),
                            (('plan', 'cycle_charge'), '1e2'), (('opening', 'basis'), 'gpu_board'),
                            (('opening', 'origin'), 'observed_telemetry'), (('reset_policy',), 'monthly_auto')):
            bad = json.loads(json.dumps(example))
            target = bad
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            self.assertTrue(validate(bad, schema, schema))
        # Unknown nested rule families must never silently become a simpler declared tariff.
        bad = json.loads(json.dumps(example))
        bad['plan']['revisions'][0]['demand_charge'] = '0'
        self.assertTrue(validate(bad, schema, schema))
        for definition in schema['$defs'].values():
            if definition.get('type') == 'object':
                self.assertFalse(definition['additionalProperties'])
                self.assertEqual(set(definition['required']), set(definition['properties']))


class StatsSchemaTests(unittest.TestCase):
    def test_schema_covers_the_writer(self):
        schema = load('config/stats-v1.schema.json')
        with open(os.path.join(ROOT, 'src', 'product', 'stats.h'), encoding='utf8') as f:
            text = f.read()
        members = set(re.findall(r'\\"([a-z_0-9]+)\\":', text))
        self.assertGreater(len(members), 30)
        schema_text = json.dumps(schema)
        missing = sorted(m for m in members if '"%s"' % m not in schema_text)
        self.assertEqual(missing, [])
        self.assertEqual(schema['properties']['schema']['const'], 'canaryminer-stats-v1')
        self.assertNotIn('wallet', schema['properties'])
        self.assertNotIn('worker', schema['properties'])


if __name__ == '__main__':
    unittest.main()
