"""Check schema rejection, reproducibility and independently specified wire bytes."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_protocol as generator
import complexity_gate


def load_projection():
    spec = importlib.util.spec_from_file_location("sentinel_protocol", ROOT / "protocol/generated/sentinel_protocol.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.source = generator.SCHEMA.read_bytes()
        self.schema = generator.projection_schema(json.loads(self.source))

    def fixture(self, name):
        fixtures = json.loads((ROOT / "tests/fixtures/wire/v1.json").read_bytes())["fixtures"]
        entry = next(item for item in fixtures if item["name"] == name)
        return b"".join(bytes.fromhex(field["hex"]) for field in entry["fields"])

    def test_deterministic_and_checked_in(self):
        first = generator.render(self.source)
        self.assertEqual(first, generator.render(self.source))
        self.assertEqual(generator.project(first, ROOT, True), [])
        canonical = json.dumps(json.loads(self.source), sort_keys=True, separators=(",", ":")).encode()
        for data in first.values():
            self.assertIn(hashlib.sha256(canonical).hexdigest().encode(), data.splitlines()[0])
            self.assertNotIn(str(ROOT).encode(), data)

    def test_schema_format_preserves_content_and_projections(self):
        schema = json.loads(self.source)
        formatted = (generator.format_schema(schema) + "\n").encode()
        self.assertEqual(formatted, self.source)
        self.assertEqual(json.loads(formatted), schema)
        self.assertEqual(formatted, (generator.format_schema(json.loads(formatted)) + "\n").encode())
        self.assertEqual(generator.render(json.dumps(schema).encode()), generator.render(formatted))
        self.assertEqual(set(schema["v1"]), {"v1_transfers", "compatibility", "v1_layouts"})

    def test_duplicate_ids_and_revisions_refused(self):
        duplicate = copy.deepcopy(self.schema)
        duplicate["commands"][1]["id"] = duplicate["commands"][0]["id"]
        with self.assertRaisesRegex(ValueError, "duplicate"):
            generator.validate(duplicate)
        self.schema["commands"][0]["payload_revisions"] = [1, 1]
        with self.assertRaisesRegex(ValueError, "revisions"):
            generator.validate(self.schema)

    def test_native_binding_coverage_and_types(self):
        missing = copy.deepcopy(self.schema)
        missing["native_bindings"].pop()
        with self.assertRaisesRegex(ValueError, "coverage"):
            generator.validate(missing)
        self.schema["native_bindings"][0]["request"] = "arbitrary()"
        with self.assertRaisesRegex(ValueError, "native request"):
            generator.validate(self.schema)

    def test_duplicate_tags_and_fields_refused(self):
        command = next(c for c in self.schema["commands"] if c["name"] == "runes")
        command["payload"]["variants"][1]["tag"] = 0
        with self.assertRaisesRegex(ValueError, "duplicate"):
            generator.validate(self.schema)
        self.schema["types"][0]["fields"].append(self.schema["types"][0]["fields"][0])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            generator.validate(self.schema)

    def test_impossible_sizes_refused(self):
        for changes in ({"count": 20000}, {"count": -1}, {"max_count": 20000}, {"type": "u8", "max": 256}):
            schema = copy.deepcopy(self.schema)
            schema["types"][0]["fields"][0].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                generator.validate(schema)

    def test_generated_source_excluded_from_handwritten_gate(self):
        with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/protocol_generated.cpp").write_text("int generated() {")
            (root / "src/active.cpp").write_text("int active() { return 0; }")
            rows = complexity_gate.scan_tree(root)
            self.assertEqual(list(rows), ["src/active.cpp::active()"])

    def test_campaign_revision_payloads_are_distinct(self):
        codec = load_projection()
        command = codec.COMMANDS[22]
        variants = [r["payload_variants"][0] for r in command["revision_types"]]
        self.assertEqual(len(set(variants)), 3)
        fields = [{f["name"] for f in codec.TYPES[name]["fields"]} for name in variants]
        self.assertEqual(fields[1] - fields[0], {"summary"})
        self.assertEqual(fields[2] - fields[1], {"rewards"})

    def test_campaign_duplicate_rewards_refused(self):
        codec = load_projection()
        data = self.fixture("campaign_rewards_row")
        value, _ = codec.decode_frame("campaign_rewards_row", data)
        value["rewards"].append(copy.deepcopy(value["rewards"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            codec.encode_frame("campaign_rewards_row", value)

    def test_check_reports_drift_without_writing(self):
        with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as directory:
            root = Path(directory)
            outputs = generator.render(self.source)
            generator.project(outputs, root, False)
            path = root / "src/protocol_generated.cpp"
            path.write_bytes(b"drift")
            before = {p.relative_to(root): (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob("*") if p.is_file()}
            result = subprocess.run([sys.executable, "-B", str(ROOT / "tools/generate_protocol.py"), "--check", "--output-root", directory], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"src/protocol_generated.cpp", result.stderr)
            after = {p.relative_to(root): (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob("*") if p.is_file()}
            self.assertEqual(before, after)

    def test_python_fixture_codecs(self):
        codec = load_projection()
        fixtures = json.loads((ROOT / "tests/fixtures/wire/v1.json").read_bytes())["fixtures"]
        for entry in fixtures:
            if "layout" not in entry:
                continue
            with self.subTest(name=entry["name"]):
                data = self.fixture(entry["name"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
                value, status = codec.decode_frame(entry["layout"], data)
                self.assertEqual(codec.encode_frame(entry["layout"], value, status), data)

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

    def test_generated_headers_refuse_corruption(self):
        codec = load_projection()
        data = self.fixture("inspect_request")
        for offset in (0, 4, 6, 8, 12, 16):
            bad = bytearray(data)
            bad[offset] = 255
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                codec.decode_frame("inspect_request", bad)

    def test_v1_fixture_bytes_and_family_coverage(self):
        fixtures = json.loads((ROOT / "tests/fixtures/wire/v1.json").read_bytes())["fixtures"]
        self.assertEqual(len({entry["name"] for entry in fixtures}), len(fixtures))
        self.assertEqual({entry["family"] for entry in fixtures}, set(self.schema["compatibility"]["families"]))
        for family in self.schema["compatibility"]["families"]:
            self.assertEqual(sorted(entry["response"] for entry in fixtures if entry["family"] == family), [False, True])
        for entry in fixtures:
            with self.subTest(name=entry["name"]):
                data = self.fixture(entry["name"])
                self.assertEqual(len(data), entry["size"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
                self.assertTrue(entry["description"])
                offset = 0
                for field in entry["fields"]:
                    raw = bytes.fromhex(field["hex"])
                    self.assertEqual(field["offset"], offset)
                    self.assertTrue(field["field"])
                    if isinstance(field["value"], int):
                        self.assertEqual(int.from_bytes(raw, "little", signed=field["type"].startswith("i")), field["value"])
                    offset += len(raw)
                self.assertEqual(offset, len(data))

    def test_invalid_v1_members_and_descriptors_refused(self):
        for change in (dict(member="value();"), dict(type="u64", count=20000)):
            schema = copy.deepcopy(self.schema)
            schema["v1_transfers"][0]["fields"][0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                generator.validate(schema)
        self.schema["compatibility"]["operations"][0]["family"] = "unknown"
        with self.assertRaisesRegex(ValueError, "unknown v1 family"):
            generator.validate(self.schema)


if __name__ == "__main__":
    unittest.main()
