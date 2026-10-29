"""Check independent v2 frames against both generated layouts and annotated offsets."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests/fixtures/wire/v2.json'
spec = importlib.util.spec_from_file_location('wire_v2_codec', ROOT / 'protocol/generated/sentinel_protocol.py')
codec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec)


class WireV2Tests(unittest.TestCase):
    def setUp(self):
        self.fixtures = {item['name']: item for item in json.loads(FIXTURES.read_bytes())['fixtures']}

    def test_offsets_hashes_and_headers(self):
        for name, item in self.fixtures.items():
            with self.subTest(name=name):
                data = b''.join(bytes.fromhex(f['hex']) for f in item['fields'])
                self.assertEqual(len(data), item['size'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), item['sha256'])
                for field in item['fields']:
                    self.assertEqual(data[field['offset']:field['offset'] + field['width']].hex(), field['hex'])
                header = codec.decode('wire_header_2', data[:32])
                self.assertEqual(header['magic'], 0x50494353)
                self.assertEqual(header['version'], 2)
                self.assertEqual(header['payload_length'], len(data) - 32)
                self.assertEqual(header['operation'], item['operation'])
                self.assertEqual(header['flags'], int(item['response']))
                self.assertEqual(codec.encode('wire_header_2', header), data[:32])

    def test_command_responses(self):
        for name, item in self.fixtures.items():
            if not item['response'] or item['operation'] == 6:
                continue
            with self.subTest(name=name):
                data = b''.join(bytes.fromhex(f['hex']) for f in item['fields'])
                fields = {f['field']:data[f['offset']:f['offset']+f['width']] for f in item['fields']}
                prefix = b''.join(fields[key] for key in ['actual_instance', 'command_reference', 'dimensions', 'command_error', 'execution'])
                response = codec.decode('command_response', prefix)
                self.assertEqual(codec.encode('command_response', response), prefix)
                self.assertEqual(response['reply']['reference']['command_id'], item['expect']['command_id'])
                self.assertEqual(response['error'], item['expect']['error'])
                for key in ['progress', 'execution', 'effect', 'retention']:
                    self.assertEqual(response['reply']['dimensions'][key], item['expect'][key])
                if fields['typed_result']:
                    result = codec.decode('sc_weapon_points_result', fields['typed_result'])
                    self.assertEqual(result['balance_after'], 3)
                    self.assertEqual(codec.encode('sc_weapon_points_result', result), fields['typed_result'])

    def test_unavailable_admission_namespace(self):
        data = bytes(156)
        result = codec.decode('sc_save_admission_snapshot', data)
        self.assertEqual(codec.encode('sc_save_admission_snapshot', result), data)
        result['namespace_id'] = [ord('a')] + [0] * 64
        with self.assertRaises(ValueError):
            codec.encode('sc_save_admission_snapshot', result)

    def test_requests_and_hello(self):
        for name, item in self.fixtures.items():
            data = b''.join(bytes.fromhex(f['hex']) for f in item['fields'])[32:]
            with self.subTest(name=name):
                if item['operation'] == 6:
                    if item['response']:
                        hello = codec.decode('hello_result', data)
                        self.assertEqual(hello['max_frame'], 16384)
                        self.assertEqual(codec.encode('hello_result', hello), data)
                    else:
                        self.assertEqual(data, b'')
                elif item['operation'] == 2 and not item['response']:
                    length = 126 + int.from_bytes(data[100:102], 'little')
                    submission = codec.decode('submission', data[:length])
                    self.assertEqual(submission['reference']['command_id'], 'fixture')
                    self.assertEqual(codec.encode('submission', submission), data[:length])
                    tag, payload = codec.decode_variant(18, 1, data[length:])
                    self.assertEqual(codec.encode_variant(18, 1, tag, payload), data[length:])
                elif not item['response']:
                    typename = 'ack' if item['operation'] == 5 else 'command_reference'
                    value = codec.decode(typename, data)
                    self.assertEqual(codec.encode(typename, value), data)


if __name__ == '__main__':
    unittest.main()
