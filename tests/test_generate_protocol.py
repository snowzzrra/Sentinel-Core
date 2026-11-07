import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def load_projection():
    spec = importlib.util.spec_from_file_location("sentinel_protocol", ROOT / "protocol/generated/sentinel_protocol.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GeneratorTests(unittest.TestCase):
    def test_check_reports_drift_without_writing(self):
        import generate_protocol as generator
        with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as directory:
            root = Path(directory)
            outputs = generator.render(generator.SCHEMA.read_bytes())
            generator.project(outputs, root, False)
            path = root / "src/protocol_generated.cpp"
            path.write_bytes(b"drift")
            before = {p.relative_to(root): (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob("*") if p.is_file()}
            result = subprocess.run([sys.executable, "-B", str(ROOT / "tools/generate_protocol.py"), "--check", "--output-root", directory], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"src/protocol_generated.cpp", result.stderr)
            after = {p.relative_to(root): (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob("*") if p.is_file()}
            self.assertEqual(before, after)


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
