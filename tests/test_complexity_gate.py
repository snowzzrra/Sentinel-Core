"""Check C++ measurements, stable function keys, and non-increasing ceilings."""
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import complexity_gate as gate


def scan(text):
    return gate.scan_source(text, "src/sample.cpp")


class ComplexityTests(unittest.TestCase):
    def test_unlisted_functions_must_meet_both_limits(self):
        rows = scan("int f() {}")
        row = next(iter(rows.values()))
        row.update(cognitive=25, cyclomatic=15)
        self.assertEqual(gate.check(rows, {}), [])
        for metric, limit in gate.LIMITS.items():
            with self.subTest(metric=metric):
                row.update(cognitive=25, cyclomatic=15)
                row[metric] = limit + 1
                self.assertEqual(len(gate.check(rows, {})), 1)

    def test_listed_functions_cannot_exceed_either_ceiling(self):
        rows = scan("int f() {}")
        key, row = next(iter(rows.items()))
        ceilings = {key: {"cognitive": 30, "cyclomatic": 20}}
        row.update(ceilings[key])
        self.assertEqual(gate.check(rows, ceilings), [])
        for metric in gate.LIMITS:
            with self.subTest(metric=metric):
                row.update(ceilings[key])
                row[metric] += 1
                self.assertEqual(len(gate.check(rows, ceilings)), 1)

    def test_update_removes_absent_and_small_functions_and_only_lowers(self):
        rows = scan("int f() {} int g() {} int h() {}")
        first, second, third = rows
        ceilings = {first: {"cognitive": 40, "cyclomatic": 20},
                    second: {"cognitive": 40, "cyclomatic": 20},
                    "removed": {"cognitive": 40, "cyclomatic": 20}}
        rows[first].update(cognitive=30, cyclomatic=22)
        rows[third].update(cognitive=50, cyclomatic=30)
        self.assertEqual(gate.lowered_ceilings(rows, ceilings),
                         {first: {"cognitive": 30, "cyclomatic": 20}})

    def test_small_and_absent_exceptions_must_be_removed(self):
        rows = scan("int f() {}")
        key = next(iter(rows))
        ceilings = {key: {"cognitive": 30, "cyclomatic": 20},
                    "removed": {"cognitive": 30, "cyclomatic": 20}}
        self.assertEqual(len(gate.check(rows, ceilings)), 2)
        self.assertEqual(gate.check(rows, gate.lowered_ceilings(rows, ceilings)), [])

    def run_gate(self, rows, ceilings, **options):
        args = gate.argparse.Namespace(write_exceptions=False, update=False,
                                       verbose=False, exceptions=Path("unused"))
        vars(args).update(options)
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(gate, "arguments", return_value=args), patch.object(gate, "scan_tree", return_value=rows):
            with patch.object(gate, "read_exceptions", return_value=ceilings), patch.object(gate, "write_exceptions") as write:
                with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                    result = gate.main()
        return result, write, output.getvalue()

    def test_failed_update_does_not_write_or_add_exceptions(self):
        rows = scan("int f() {}")
        next(iter(rows.values()))["cognitive"] = 26
        result, write, _ = self.run_gate(rows, {}, update=True)
        self.assertEqual(result, 1)
        write.assert_not_called()

    def test_update_writes_lowered_exceptions(self):
        rows = scan("int f() {}")
        key, row = next(iter(rows.items()))
        row.update(cognitive=26, cyclomatic=10)
        result, write, _ = self.run_gate(rows, {key: {"cognitive": 30, "cyclomatic": 20}}, update=True)
        self.assertEqual(result, 0)
        write.assert_called_once_with(Path("unused"), {key: {"cognitive": 26, "cyclomatic": 10}})

    def test_update_rejects_growth_even_when_function_drops_within_limits(self):
        rows = scan("int f() {}")
        key, row = next(iter(rows.items()))
        row.update(cognitive=20, cyclomatic=2)
        result, write, _ = self.run_gate(rows, {key: {"cognitive": 30, "cyclomatic": 1}}, update=True)
        self.assertEqual(result, 1)
        write.assert_not_called()

    def test_update_removes_functions_within_limits(self):
        rows = scan("int f() {}")
        key = next(iter(rows))
        result, write, _ = self.run_gate(rows, {key: {"cognitive": 30, "cyclomatic": 20}}, update=True)
        self.assertEqual(result, 0)
        write.assert_called_once_with(Path("unused"), {})

    def test_explicit_regeneration_only_records_functions_above_limits(self):
        rows = scan("int f() {} int g() {}")
        key = next(iter(rows))
        rows[key].update(cognitive=26, cyclomatic=10)
        result, write, _ = self.run_gate(rows, {}, write_exceptions=True)
        self.assertEqual(result, 0)
        write.assert_called_once_with(Path("unused"), {key: {"cognitive": 26, "cyclomatic": 10}})

    def test_warnings_require_verbose(self):
        rows = scan("int f() {}")
        next(iter(rows.values()))["cognitive"] = 16
        self.assertNotIn("WARN", self.run_gate(rows, {})[2])
        self.assertIn("WARN", self.run_gate(rows, {}, verbose=True)[2])

    def test_sorted_text_round_trip_and_empty_list(self):
        rows = scan("int z(int x) {} int a() {}")
        ceilings = {key: {"cognitive": 26, "cyclomatic": 10} for key in rows}
        with patch.object(Path, "write_text") as write:
            gate.write_exceptions(Path("unused"), ceilings)
        contents = write.call_args.args[0]
        self.assertEqual(contents.splitlines()[1:], sorted(contents.splitlines()[1:]))
        for text, expected in ((contents, ceilings), (gate.HEADER, {})):
            with patch.object(Path, "read_text", return_value=text):
                self.assertEqual(gate.read_exceptions(Path("unused")), expected)

    def test_invalid_or_unneeded_exceptions_fail_closed(self):
        for line in ("src/a.cpp::f() 25 15", "src/a.cpp::f() -1 20", "broken", "src/a.cpp::f() x 20"):
            with self.subTest(line=line), patch.object(Path, "read_text", return_value=gate.HEADER + line):
                with self.assertRaises(ValueError):
                    gate.read_exceptions(Path("unused"))

    def test_signature_keys_keep_normalization(self):
        self.assertEqual(scan("namespace n { int f(int x) { return x; } }").keys(),
                         scan("namespace n{int f ( int x ){if(x)return x;return 0;}}").keys())
        self.assertIn("src/sample.cpp::n::f(int x)", scan("namespace n { int f(int x) {} }"))


if __name__ == "__main__":
    unittest.main()
