"""Shared evidence payloads reject unsupported grants."""
import importlib.util
import json
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('evidence_codec', ROOT / 'protocol/generated/sentinel_protocol.py')
codec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec)


class EvidenceWireTests(unittest.TestCase):
    def test_result_fixtures(self):
        fixtures = json.loads((ROOT / 'tests/fixtures/wire/evidence_results.json').read_text())['fixtures']
        for fixture in fixtures:
            data = bytes.fromhex(fixture['hex'])
            self.assertEqual(len(data), fixture['size'])
            self.assertEqual(hashlib.sha256(data).hexdigest(), fixture['sha256'])
            kind = fixture['type']
            with self.subTest(name=fixture['name']):
                if kind.endswith('_frame'):
                    header = codec.decode('wire_header_2', data[:32])
                    self.assertEqual(header['payload_length'], len(data) - 32)
                    self.assertEqual(codec.encode('wire_header_2', header), data[:32])
                    if kind == 'read_frame':
                        codec.decode('scope', data[32:68])
                        typed = {'currency_observation': 'sc_currency_result', 'progression_observation': 'sc_progression_result', 'mission_access_observation': 'sc_mission_access_result'}[fixture['name']]
                        result = codec.decode(typed, data[68:])
                        self.assertEqual(codec.encode(typed, result), data[68:])
                    else:
                        prefix = 32 + 190 + 7 + 424
                        envelope = codec.decode('command_response', data[32:prefix])
                        self.assertEqual(envelope['reply']['reference']['command_type'], 53)
                        self.assertEqual(envelope['reply']['reference']['command_id'], 'battery')
                        result = codec.decode('sc_currency_result', data[prefix:])
                        self.assertEqual(result['balance_before'], 0)
                        self.assertEqual(result['gained_after'], 9)
                elif kind.endswith('_payload'):
                    tag, value = codec.decode_variant(fixture['command'], 1, data)
                    self.assertEqual(codec.encode_variant(fixture['command'], 1, tag, value), data)
                else:
                    result = codec.decode(kind, data)
                    self.assertEqual(codec.encode(kind, result), data)
                    if kind == 'sc_progression_result':
                        self.assertEqual(result['mastery_proof'], [0] * 13)
                        self.assertEqual(result['suit_owned'], 45)
                    if kind == 'sc_mission_access_result':
                        self.assertGreater(result['destination_scope']['process']['lifecycle_generation'], result['menu_scope']['process']['lifecycle_generation'])

    def test_currency_fixtures(self):
        fixtures = json.loads((ROOT / 'tests/fixtures/wire/evidence.json').read_text())['fixtures']
        for fixture in fixtures:
            data = bytes.fromhex(fixture['hex'])
            tag, value = codec.decode_variant(53, 1, data)
            self.assertEqual(codec.encode_variant(53, 1, tag, value), data)
        for currency in (0, 1, 2, 3):
            with self.assertRaises(ValueError):
                codec.encode_variant(53, 1, 1, dict(currency=currency, amount=1, expected_gained=0))
        with self.assertRaises(ValueError):
            codec.encode_variant(53, 1, 1, dict(currency=4, amount=2, expected_gained=2147483647))


if __name__ == '__main__':
    unittest.main()
