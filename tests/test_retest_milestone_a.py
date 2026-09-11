"""Retest workflow fixtures: no actual game, Steam, saves or installed DLL access.

Only fixture copies substitute stopped-process checks. Process identity and the
Steam launch boundary are injected in tests; real protection, preparation probe,
PowerShell arrays, bounded subprocess capture and ZIP export still run.
"""
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "Sentinel-Core/tools"
PYTHON = Path(r"C:\Users\guimo\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe")
POWERSHELL = Path(r"C:\Users\guimo\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe")
sys.path.insert(0, str(TOOLS))
try:
    spec = importlib.util.spec_from_file_location("retest", TOOLS / "retest_milestone_a.py")
    retest = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(retest)
finally:
    sys.path.pop(0)


@unittest.skipUnless(os.name == "nt", "Windows protected source and PowerShell7 contract")
class RetestWorkflowTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="retest fixture ", dir=Path(__file__).parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.candidate = self.root / "explicit candidate"
        self.candidate.mkdir()
        self.game = self.root / "game install"
        self.game.mkdir()
        self.steam = self.root / "Steam fixture" / "userdata" / "12345" / "782330"
        self.local = self.root / "local provider"
        self.local.mkdir()
        (self.steam / "remote" / "GAME-AUTOSAVE0").mkdir(parents=True)
        self.campaign = self.steam / "remote" / "GAME-AUTOSAVE0" / "game.details"
        self.campaign.write_bytes(b"private fixture campaign bytes DO_NOT_EXPORT")
        (self.steam / "remote" / "PROFILE").write_bytes(b"private fixture profile DO_NOT_EXPORT")
        (self.local / "settings.cfg").write_bytes(b"private fixture settings")
        for name in retest.REQUIRED:
            source = TOOLS / name
            if name.endswith((".exe", ".dll")):
                source = ROOT / "Sentinel-Core/build/bin" / name
            elif name == "Prepare-MilestoneA.ps1":
                source = ROOT / "SentinelDocs/tooling" / name
            shutil.copy2(source, self.candidate / name)
        helper_path = self.candidate / "prepare_vanilla_backup.py"
        helper = helper_path.read_text(encoding="utf-8")
        marker = '\nif __name__ == "__main__":'
        helper = helper.replace(marker, '\n# TEST FIXTURE ONLY; production has no bypass.\ndef require_stopped():\n    pass\n' + marker)
        helper_path.write_text(helper, encoding="utf-8")
        for name in ("msimg32.dll", "sentinel_core.dll"):
            shutil.copy2(self.candidate / name, self.game / name)
        (self.game / "DOOMEternalx64vk.exe").write_bytes(b"fixture game identity; never executed")
        steam_exe = self.root / "Steam fixture" / "steam.exe"
        steam_exe.write_bytes(b"fixture launcher; never executed")
        self.manifest = {"status": "MILESTONE_A_READY_FOR_GUARDED_SMOKE", "product_version": "0.6.0",
                         "build_id": "a" * 64, "supported_game_sha256": retest.sha(self.game / "DOOMEternalx64vk.exe"),
                         "required_routes": 63,
                         "files": [{"name": p.name, "sha256": retest.sha(p)} for p in self.candidate.iterdir()]}
        retest.write_json(self.candidate / "manifest.json", self.manifest, create=True)
        self.config = {"GameInstall": str(self.game), "SteamExe": str(steam_exe), "SteamAccount32": "12345",
                       "SteamAppRoot": str(self.steam), "LocalProviderRoot": str(self.local),
                       "OriginalBackupDirectory": str(self.root / "first backup"), "APRoot": str(self.root / "ap"),
                       "UninstallRoots": [str(self.game), str(self.root / "separate mod install")],
                       "Python": str(PYTHON), "PowerShell7": str(POWERSHELL),
                       "CandidateManifest": str(self.candidate / "manifest.json"), "EvidenceRoot": str(self.root / "evidence"),
                       "ActiveRun": str(self.root / "exact active run.json")}
        self.config_path = self.root / "config.private.json"
        retest.write_json(self.config_path, self.config, create=True)
        self.observed = {"pid": 45678, "process_created": "134077788800000000", "path": str(self.game / "DOOMEternalx64vk.exe"),
                         "modules": [{"basename": name, "path": str(self.game / name), "sha256": retest.sha(self.game / name)}
                                     for name in ("sentinel_core.dll", "msimg32.dll")]}
        self.launches = []
        self.queries = []
        self.query_responses = {}
        self.real_runner = retest.run_command
        self.stop_patch = mock.patch.object(retest.protection, "require_stopped")
        self.stopped = self.stop_patch.start()
        self.addCleanup(self.stop_patch.stop)
        self.process_patch = mock.patch.object(retest, "observe_game", side_effect=lambda *unused: copy.deepcopy(self.observed))
        self.process_mock = self.process_patch.start()
        self.addCleanup(self.process_patch.stop)
        self.launch_patch = mock.patch.object(retest, "launch_steam", side_effect=self.fake_launch)
        self.launch_patch.start()
        self.addCleanup(self.launch_patch.stop)
        self.command_patch = mock.patch.object(retest, "run_command", side_effect=self.fixture_command)
        self.command_patch.start()
        self.addCleanup(self.command_patch.stop)

    def fake_launch(self, config, descriptor):
        self.assertTrue(Path(descriptor).is_file())
        self.assertTrue((Path(config["OriginalBackupDirectory"]) / "vanilla-protection.json").is_file())
        self.launches.append(descriptor)
        return 78901

    def fixture_command(self, command, prefix, timeout):
        if str(command[0]) == str(self.candidate / "sentinel_probe.exe") and "--pid" in command:
            query = next((q for q in retest.QUERIES[1:] if "--" + q.replace("_", "-") in command), "basic")
            self.queries.append(query)
            response = {"result": "ok", "operation": query, "target_pid": self.observed["pid"], "server_pid": self.observed["pid"],
                        "process_created": self.observed["process_created"], "instance_id": "b" * 32, "build_id": self.manifest["build_id"],
                        "host_path": str(self.game), "private_secret": "DO_NOT_EXPORT"}
            code = 0
            if query == "save_admission":
                response.update(state="disabled", fault="native_initialization_failed", prepared_routes=0, required_routes=63)
                code = 8
            if query == "save_installation":
                response["installation"] = {"abi": 1, "clock": "GetTickCount64", "units": "milliseconds", "attempt": 1,
                    "phase": "failed", "validated": 2, "created": 0, "enabled": 0, "active": None,
                    "primary_failure": {"stage": "save_target", "target_group": 2, "target_index": 2,
                                        "target_name": "fixture_target", "rva": 100, "reason": "signature_mismatch",
                                        "minhook_status": None, "win32_error": None, "byte_count": 2, "collision_rva": 0,
                                        "expected_bytes": "aabb", "actual_bytes": "ccdd",
                                        "secret": "DO_NOT_EXPORT"}, "private_secret": "DO_NOT_EXPORT"}
            custom = self.query_responses.get(query)
            if custom:
                response, code = custom(response, code)
            payload = json.dumps(response) if isinstance(response, dict) else response
            script = "import sys; sys.stdout.write(" + repr(payload) + "); sys.stderr.write('private stderr DO_NOT_EXPORT'); sys.exit(" + str(code) + ")"
            return self.real_runner([str(PYTHON), "-B", "-c", script], prefix, timeout)
        return self.real_runner(command, prefix, timeout)

    def stage(self, name, *arguments):
        output = io.StringIO()
        # Independent invocation loads only disk state, from an unrelated cwd.
        old_cwd = Path.cwd()
        os.chdir(self.root)
        try:
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                result = retest.main(["--config", str(self.config_path), name, *arguments])
        finally:
            os.chdir(old_cwd)
        return result, output.getvalue()

    def state(self):
        reference = retest.read_json(self.config["ActiveRun"])
        directory = Path(reference["run_directory"])
        return retest.read_json(directory / "private/state.json"), directory

    def test_start_capture_finish_array_paths_safe_queries_and_allowlisted_zip(self):
        self.assertEqual(self.stage("START")[0], 0)
        self.assertEqual(len(self.launches), 1)
        state, directory = self.state()
        params = retest.read_json(directory / "private/preparation-parameters.json")
        self.assertEqual(params["UninstallRoot"], self.config["UninstallRoots"])
        self.assertIsInstance(params["UninstallRoot"], list)
        self.assertEqual(self.stage("CAPTURE")[0], 0)
        self.assertEqual(self.queries, list(retest.QUERIES))
        result, output = self.stage("FINISH", "--catalog", "unchanged", "--selection", "not_observed", "--note", self.config["SteamAppRoot"] + " password=private")
        self.assertEqual(result, 0, output)
        bundle = next(directory.glob("*-shareable.zip"))
        with zipfile.ZipFile(bundle) as archive:
            self.assertEqual(set(archive.namelist()), {"SUMMARY.md", "report.json"})
            text = archive.read("report.json").decode()
            report = json.loads(text)
            self.assertNotIn("DO_NOT_EXPORT", text)
            self.assertNotIn(self.config["SteamAppRoot"], text)
            self.assertNotIn("password=private", text)
            self.assertNotIn("host_path", text)
            self.assertEqual(report["comparison"]["response"]["result"], "vanilla_campaign_unchanged")
            self.assertEqual(report["admission_observation"], "rejected_or_unavailable_no_gameplay")
            admission = next(c for c in report["commands"] if c["query"] == "save_admission")
            self.assertEqual(admission["exit_code"], 8)
            detail = next(c for c in report["commands"] if c["query"] == "save_installation")
            self.assertEqual(detail["response"]["installation"]["primary_failure"]["reason"], "signature_mismatch")
        before = bundle.read_bytes()
        share_before = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in (directory / "shareable").iterdir()}
        self.assertEqual(self.stage("FINISH")[0], 1)
        self.assertEqual(before, bundle.read_bytes())
        self.assertEqual(share_before, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in (directory / "shareable").iterdir()})

    def test_wrong_admission_namespace_or_root_keeps_safe_queries_but_refuses_eligibility(self):
        self.assertEqual(self.stage("START")[0], 0)
        state, directory = self.state()
        namespace = state["namespace_id"]
        for wrong_field in ("namespace_id", "native_root"):
            with self.subTest(wrong_field=wrong_field):
                def wrong_namespace(response, code):
                    response.update(state="admitted", accepting_requests=True, prepared_routes=63,
                                    namespace_id=namespace, native_root="ap-" + namespace[:40])
                    response[wrong_field] = "c" * 64 if wrong_field == "namespace_id" else "ap-" + "c" * 40
                    return response, 0
                self.query_responses["save_admission"] = wrong_namespace
                result, output = self.stage("CAPTURE")
                self.assertEqual(result, 1)
                self.assertIn("AP_namespace_mismatch_remaining_safe_queries_captured", output)
                self.assertEqual(self.queries[-len(retest.QUERIES):], list(retest.QUERIES))
                current, _ = self.state()
                self.assertEqual(next(item for item in reversed(current["commands"]) if item["query"] == "save_admission")["namespace_state"], "mismatch")
        self.assertFalse((directory / "shareable").exists())
        self.assertFalse(list(directory.glob("*-shareable.zip")))
        self.assertEqual(self.stage("FINISH", "--comparison-cancelled")[0], 0)
        report = retest.read_json(directory / "shareable/report.json")
        self.assertEqual(report["admission_observation"], "rejected_or_unavailable_no_gameplay")

    def test_matching_admission_namespace_is_explicitly_verified(self):
        self.assertEqual(self.stage("START")[0], 0)
        state, directory = self.state()
        namespace = state["namespace_id"]
        def matching_namespace(response, code):
            response.update(state="admitted", accepting_requests=True, prepared_routes=63,
                            namespace_id=namespace, native_root="ap-" + namespace[:40])
            return response, 0
        self.query_responses["save_admission"] = matching_namespace
        self.assertEqual(self.stage("CAPTURE")[0], 0)
        self.assertEqual(self.stage("FINISH", "--comparison-cancelled")[0], 0)
        report = retest.read_json(directory / "shareable/report.json")
        self.assertEqual(report["admission_observation"], "admitted_in_read_only_snapshot")
        self.assertEqual(next(item for item in report["commands"] if item["query"] == "save_admission")["namespace_state"], "verified")

    def test_candidate_mismatch_precedes_protection_and_launch(self):
        (self.game / "sentinel_core.dll").write_bytes(b"unmatched fixture DLL")
        result, output = self.stage("START")
        self.assertEqual(result, 1)
        self.assertIn("installed_candidate_mismatch", output)
        self.assertFalse(self.launches)
        self.assertFalse(Path(self.config["APRoot"]).exists())
        self.assertFalse(Path(self.config["OriginalBackupDirectory"]).exists())
        self.assertEqual(self.stage("FINISH", "--comparison-cancelled")[0], 0)

    def test_failed_protection_does_not_prepare_or_launch_and_still_exports(self):
        with self.campaign.open("rb"):
            self.assertEqual(self.stage("START")[0], 1)
        self.assertFalse(self.launches)
        self.assertFalse(Path(self.config["APRoot"]).exists())
        self.assertFalse(Path(self.config["OriginalBackupDirectory"]).exists())
        self.stopped.side_effect = retest.protection.Refused("exit DOOM and Steam")
        self.assertEqual(self.stage("FINISH")[0], 0)
        _, directory = self.state()
        report = retest.read_json(directory / "shareable/report.json")
        self.assertEqual(report["comparison"]["state"], "pending")

    def test_process_replacement_stops_without_merging_second_instance(self):
        self.assertEqual(self.stage("START")[0], 0)
        def replacing(*unused):
            observed = copy.deepcopy(self.observed)
            if self.queries:
                observed["process_created"] = "134077788800000001"
            return observed
        self.process_mock.side_effect = replacing
        result, output = self.stage("CAPTURE")
        self.assertEqual(result, 1)
        self.assertIn("game_process_replaced", output)
        self.assertEqual(self.queries, ["basic"])
        state, _ = self.state()
        self.assertEqual(state["process"]["process_created"], self.observed["process_created"])

    def test_partial_query_is_preserved_and_remaining_queries_continue(self):
        self.assertEqual(self.stage("START")[0], 0)
        self.query_responses["native"] = lambda response, code: ('{"result":', 3)
        self.assertEqual(self.stage("CAPTURE")[0], 0)
        self.assertEqual(self.queries, list(retest.QUERIES))
        self.assertEqual(self.stage("FINISH", "--comparison-cancelled")[0], 0)
        _, directory = self.state()
        report = retest.read_json(directory / "shareable/report.json")
        native = next(c for c in report["commands"] if c["query"] == "native")
        self.assertEqual(native["exit_code"], 3)
        self.assertEqual(native["json_state"], "missing_or_partial")
        self.assertEqual(report["comparison"]["state"], "cancelled")

    def test_cancelled_capture_is_not_success_and_FINISH_still_exports(self):
        self.assertEqual(self.stage("START")[0], 0)
        real_fixture = self.fixture_command
        def cancelling(command, prefix, timeout, **kwargs):
            result = real_fixture(command, prefix, timeout)
            if "--engine" in command:
                result.update(cancelled=True, exit_code=None)
            return result
        with mock.patch.object(retest, "run_command", side_effect=cancelling):
            self.assertEqual(self.stage("CAPTURE")[0], 1)
        self.assertEqual(self.queries, ["basic", "engine"])
        self.assertEqual(self.stage("FINISH", "--comparison-cancelled")[0], 0)
        _, directory = self.state()
        report = retest.read_json(directory / "shareable/report.json")
        capture = next(stage for stage in report["stages"] if stage["stage"] == "CAPTURE")
        self.assertEqual(capture["state"], "cancelled")
        self.assertIn("read-only query: save_installation", report["not_performed"])

    def test_command_timeout_truncation_and_launch_error_remain_distinct(self):
        result = self.real_runner([str(PYTHON), "-B", "-c", "import time; print('partial',flush=True); time.sleep(3)"], self.root / "timeout", 0.1)
        self.assertTrue(result["timed_out"])
        self.assertIsNone(result["exit_code"])
        large = self.real_runner([str(PYTHON), "-B", "-c", "print('x'*300000)"], self.root / "truncated", 5)
        self.assertTrue(large["stdout_truncated"])
        self.assertEqual(large["exit_code"], 0)
        self.assertEqual(large["stdout_bytes_retained"], retest.MAX_OUTPUT)
        missing = self.real_runner([str(self.root / "absent.exe")], self.root / "absent", 5)
        self.assertEqual(missing["launch_error"], "command_launch_failed")
        self.assertFalse(missing["timed_out"])
        self.assertIsNone(missing["exit_code"])

    def test_optional_debug_worker_is_bounded_private_and_does_not_accept_EULA(self):
        private = self.root / "debug private fixture"
        private.mkdir()
        config = {**self.config, "DebugView": {"Requested": True, "Path": str(self.root / "fixture capture executable.exe"), "Sha256": "c" * 64}}
        path = private / "config.json"
        retest.write_json(path, config, create=True)
        commands = []
        def capture_command(command, prefix, timeout, started=None):
            commands.append(command)
            self.assertEqual(timeout, 190)
            process = mock.Mock(pid=11111)
            process.poll.return_value = None
            started(process)
            return {"exit_code": 0, "timed_out": False, "cancelled": False, "stdout": "private DO_NOT_EXPORT", "stderr": ""}
        with mock.patch.object(retest, "debug_prerequisite", return_value=None), \
             mock.patch.object(retest, "debug_owner_present", return_value=True), \
             mock.patch.object(retest, "run_command", side_effect=capture_command):
            retest.debug_worker(path, private)
        self.assertEqual(len(commands), 1)
        command = commands[0]
        self.assertNotIn("--accepteula", command)
        self.assertEqual(command[command.index("--duration") + 1], "180")
        self.assertEqual(command[command.index("--max-lines") + 1], "32")
        self.assertEqual(command[command.index("--process-filter") + 1], "DOOMEternalx64vk.exe")
        self.assertTrue(all(flag in command for flag in ("--win32", "--no-kernel", "--no-global")))
        self.assertEqual(retest.read_json(private / "debug-ready.private.json")["state"], "armed")
        self.assertNotIn("stdout", retest.read_json(private / "debug-finished.private.json"))
        with mock.patch.object(retest, "debug_prerequisite", return_value="optional_capture_EULA_not_already_accepted"), \
             mock.patch.object(retest.subprocess, "Popen") as process:
            self.assertEqual(retest.arm_debug(config, private)["state"], "unavailable")
            process.assert_not_called()

    def test_installation_actual_schema_preserves_active_null_and_negative_API_status(self):
        event = {"sequence": "4", "at_ms": "1234", "duration_ms": "2", "stage": "save_create", "target_group": 2,
                 "target_index": 0, "target_name": "save_target_0", "rva": 123, "signature_offset": 0,
                 "result": "failed", "reason": "hook_create_failed", "read_reason": None, "win32_error": 0,
                 "minhook_status": -1, "byte_count": 2, "byte_window_offset": 64, "collision_rva": 321, "expected_bytes": "aabb", "actual_bytes": "ccdd"}
        detail = {"abi": 1, "clock": "GetTickCount64", "units": "milliseconds", "attempt": 1, "sequence": "4",
                  "phase": "failed", "last_completed_stage": "save_binding", "startup_observation": "not_observed",
                  "validated": 48, "created": 3, "enabled": 3, "cleanup_failures": 0, "gaps": 0,
                  "active": event, "primary_failure": event, "cleanup_failure": None}
        self.assertEqual(retest.safe_response("save_installation", {"installation": detail})["installation"], detail)

    def test_completed_run_is_retained_when_explicit_START_creates_next_run(self):
        self.assertEqual(self.stage("START")[0], 0)
        self.assertEqual(self.stage("FINISH", "--comparison-cancelled")[0], 0)
        first, first_directory = self.state()
        bundle = next(first_directory.glob("*-shareable.zip"))
        original = bundle.read_bytes()
        self.assertEqual(self.stage("START")[0], 0)
        second, second_directory = self.state()
        self.assertNotEqual(first["run_id"], second["run_id"])
        self.assertNotEqual(first_directory, second_directory)
        self.assertEqual(original, bundle.read_bytes())
        self.assertEqual(self.stage("START")[0], 1)

    def test_real_powershell_entry_from_unrelated_directory_uses_disk_config(self):
        self.assertEqual(self.stage("START")[0], 0)
        state, directory = self.state()
        # A real independent shell only exports a cancelled comparison; it never
        # invokes START, accesses original payloads, discovers games or launches Steam.
        entry = self.root / "Retest-MilestoneA.ps1"
        shutil.copy2(ROOT / "SentinelDocs/Retest-MilestoneA.ps1", entry)
        shutil.copy2(self.config_path, self.root / "milestone-a-retest.private.json")
        completed = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-File",
            str(entry), "-Stage", "FINISH",
            "-ComparisonCancelled", "-Catalog", "unchanged", "-Note", "fixture operator observation"],
            cwd=self.root, capture_output=True, text=True, timeout=15)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        report = retest.read_json(directory / "shareable/report.json")
        self.assertEqual(report["operator"]["catalog"], "unchanged")
        self.assertEqual(report["comparison"]["state"], "cancelled")


if __name__ == "__main__":
    unittest.main()
