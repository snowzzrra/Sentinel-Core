import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def load_projection():
    spec = importlib.util.spec_from_file_location("sentinel_protocol", ROOT / "protocol/generated/sentinel_protocol.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GeneratorTests(unittest.TestCase):


    def test_closed_variants_signed_selection_and_bounds(self):
        codec = load_projection()
        data = codec.encode_variant(33, 1, 3, {"slot": 2, "rune": -1})
        self.assertEqual(data, b"\x03\0\0\0\x02\xff")
        self.assertEqual(codec.decode_variant(33, 1, data), (3, {"slot": 2, "rune": -1}))
        for command, revision, tag, payload in ((33, 1, 99, {}), (33, 2, 3, {}), (18, 1, 1, {"amount": 118, "expected_gained": 0}), (18, 1, 1, {"amount": 1, "expected_gained": 2147483647})):
            with self.assertRaises(ValueError):
                codec.encode_variant(command, revision, tag, payload)
        with self.assertRaises(ValueError):
            codec.decode_variant(33, 1, b"\xff\xff\xff\xff")


if __name__ == "__main__":
    unittest.main()
