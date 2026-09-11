"""Retest workflow fixtures: no actual game, Steam, saves or installed DLL access.

Only fixture copies substitute stopped-process checks. Process discovery and IPC
are injected; real protection, preparation probe, one-use lease, PowerShell,
bounded subprocess capture and ZIP export still run. Nothing launches Steam.
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
        self.actual_observe_game = retest.observe_game
        self.process_patch = mock.patch.object(retest, "observe_game", side_effect=lambda *unused: copy.deepcopy(self.observed))
        self.process_mock = self.process_patch.start()
        self.addCleanup(self.process_patch.stop)
        self.system_forwarder = str(Path(os.environ['SystemRoot']) / 'System32/msimg32.dll')
        self.module_observed = {'modules': copy.deepcopy(self.observed['modules']) +
            [{'basename': 'msimg32.dll', 'path': self.system_forwarder, 'sha256': 'c' * 64}],
            'system_path': self.system_forwarder, 'system_sha256': 'c' * 64}
        module_patch = mock.patch.object(retest, 'observe_modules', side_effect=lambda *unused: copy.deepcopy(self.module_observed))
        self.module_mock = module_patch.start()
        self.addCleanup(module_patch.stop)
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

    def closed_after_capture(self, config, prefix):
        if "observation-" in str(prefix):
            raise retest.Refused("no_game_process")
        return copy.deepcopy(self.observed)

    def report(self, directory):
        with zipfile.ZipFile(directory / (directory.name + "-shareable.zip")) as archive:
            self.assertEqual(set(archive.namelist()), {"SUMMARY.md", "report.json"})
            text = archive.read("report.json").decode()
            self.assertNotIn("DO_NOT_EXPORT", text)
            self.assertNotIn(str(self.local), text)
            self.assertNotIn(str(self.steam), text)
            return json.loads(text)

    def update_member(self, name):
        for entry in self.manifest["files"]:
            if entry["name"] == name: entry["sha256"] = retest.sha(self.candidate / name)
        retest.write_json(self.candidate / "manifest.json", self.manifest)

    def test_run_protection_native_refusal_all_queries_and_one_report(self):
        self.process_mock.side_effect = self.closed_after_capture
        self.query_responses["save_admission"] = lambda response, code: ({**response, "state": "rejected"}, 8)
        code, output = self.stage("RUN")
        state, directory = self.state()
        self.assertTrue(state.get("protection"), output)
        self.assertEqual(self.queries, list(retest.QUERIES))
        self.assertEqual(self.launches, [])
        self.assertFalse((self.game / "sentinel-prelaunch.txt").exists())
        report = self.report(directory)
        self.assertEqual(report["preparation"]["state"], "prepared")
        self.assertEqual(report["admission_observation"], "native_refusal_observed")
        self.assertEqual(report["hook_installation"], "failed")
        self.assertEqual(report["comparison"]["response"]["result"], "vanilla_campaign_unchanged")
        self.assertEqual(len(list(directory.glob("*.zip"))), 1)
        self.assertEqual(code, 1)  # Native refusal remains evidence, never a PASS.

    def test_run_survives_transient_identity_collection_and_exports_after_exit(self):
        observations = iter([retest.CollectionUnavailable('process_identity', {'exit_code': None, 'timed_out': True})] * 3 +
                            [copy.deepcopy(self.observed), retest.Refused('no_game_process')])
        def observe(config, prefix):
            value = next(observations) if 'observation-' in prefix.name else copy.deepcopy(self.observed)
            if isinstance(value, Exception): raise value
            return value
        self.process_mock.side_effect = observe
        with mock.patch.object(retest.time, 'sleep'):
            code, output = self.stage('RUN')
        state, directory = self.state()
        self.assertEqual(code, 1)  # Retained native refusal, never a collection/DOOM PASS.
        self.assertEqual(state['process']['pid'], self.observed['pid'], output)
        self.assertEqual(state['collection_health']['operations']['process_identity']['failures'], 3)
        self.assertEqual(state['collection_health']['operations']['process_identity']['recoveries'], 1)
        self.assertEqual(self.report(directory)['comparison']['response']['result'], 'vanilla_campaign_unchanged')
        self.assertFalse((self.game / 'sentinel-prelaunch.txt').exists())

    def test_successive_runs_refresh_current_metadata_preserve_first_and_compare_current(self):
        self.assertEqual(self.stage("PREPARE")[0], 0)
        first_state, first_run = self.state()
        first = Path(self.config["OriginalBackupDirectory"])
        hashes = {str(p.relative_to(first)): retest.sha(p) for p in first.rglob("*") if p.is_file()}
        (self.local / "settings.cfg").write_bytes(b"legitimate changed configuration")
        stamp = self.campaign.stat().st_mtime_ns
        os.utime(self.campaign, ns=(stamp, stamp+1000000000))
        self.process_mock.side_effect = self.closed_after_capture
        self.stage("RUN")
        state, directory = self.state()
        self.assertNotEqual(state["protection"]["reference_directory"], str(first))
        self.assertEqual(hashes, {str(p.relative_to(first)): retest.sha(p) for p in first.rglob("*") if p.is_file()})
        self.assertTrue((first_run / (first_run.name+"-shareable.zip")).is_file())
        report = self.report(directory)
        counts = report["preparation"]["historical_comparison"]["counts"]
        self.assertEqual(counts["content_changed"], 1)
        self.assertEqual(counts["metadata_only"], 1)
        self.assertEqual(report["comparison"]["response"]["modified"], 0)

    def test_previous_campaign_regression_cannot_be_normalized_by_next_reference(self):
        def observe(config, prefix):
            if "observation-" in str(prefix):
                self.campaign.write_bytes(b"unexpected campaign regression")
            return self.closed_after_capture(config, prefix)
        self.process_mock.side_effect = observe
        self.stage("RUN")
        previous, directory = self.state()
        self.assertEqual(previous["comparison"]["response"]["modified"], 1)
        old_report = (directory / (directory.name+"-shareable.zip")).read_bytes()
        code, output = self.stage("PREPARE")
        state, refused = self.state()
        self.assertEqual(code, 1, output)
        self.assertNotIn("protection", state)
        self.assertIn("previous protected campaign regression", output)
        self.assertEqual(old_report, (directory / (directory.name+"-shareable.zip")).read_bytes())
        self.assertEqual(self.report(refused)["admission_observation"], "NOT_TESTED")

    def test_pending_previous_comparison_is_recovered_before_new_protection(self):
        self.process_mock.side_effect = self.closed_after_capture
        self.stage("RUN")
        previous, directory = self.state()
        previous["comparison"] = {"state": "not_performed", "reason": "fixture_interruption"}
        retest.write_json(directory / "private/state.json", previous)
        self.campaign.write_bytes(b"unexplained delayed campaign change")
        code, output = self.stage("PREPARE")
        state, refused = self.state()
        self.assertEqual(code, 1, output)
        outcome = state["previous_comparisons"][0]
        self.assertEqual(outcome["recovered_comparison"]["modified"], 1)
        self.assertNotIn("protection", state)
        self.report(refused)

    def test_failed_preparation_is_not_native_rejection_and_retains_os_error(self):
        with self.campaign.open("rb"):
            code, output = self.stage("RUN")
        state, directory = self.state()
        report = self.report(directory)
        self.assertEqual(code, 1, output)
        self.assertEqual(report["admission_observation"], "NOT_TESTED")
        self.assertEqual(report["hook_installation"], "NOT_TESTED")
        self.assertEqual(report["game_observation"], "NOT_OBSERVED")
        self.assertEqual(report["primary_failure"]["stage"], "exclusive_acquisition")
        self.assertEqual(report["primary_failure"]["win32_error"], 32)
        self.assertFalse(self.queries)

    def test_profile_failure_survives_module_visibility_recovery(self):
        run = retest.Run(self.config_path, "RUN")
        run.prepare()
        run.state["process"] = copy.deepcopy(self.observed)
        run.state["process"]["instance_id"] = None
        self.query_responses["save_installation"] = lambda r, c: ({**r, "installation": {"phase": "installation_ready", "primary_failure": None}}, 0)
        self.query_responses["save_admission"] = lambda r, c: ({**r, "state": "faulted", "fault": "native_profile", "prepared_routes": 63}, 8)
        # Exact per-process log; foreign keys/payloads must never reach the ZIP.
        diagnostic = self.root / "log-root/SentinelCore/diagnostics"
        diagnostic.mkdir(parents=True)
        record = {"schema": "sentinel-startup-v1", "pid": self.observed["pid"], "process_created": self.observed["process_created"],
                  "build_id": self.manifest["build_id"], "admission": {"state": 5, "fault": 15}, "profile": {
                      "request_id": 1, "first_failed_stage": "decode", "private_identity": "DO_NOT_EXPORT",
                      "first_failure": {"status": 4, "predicate": "native_authentication_refused", "native_attempted": True,
                                        "native_outcome": 1, "native_value": 4, "payload": "DO_NOT_EXPORT"},
                      "steps": {"decode": {"status": 4, "predicate": "native_authentication_refused"}}}}
        earlier = {**record, "profile": {}, "unknown_padding": "x" * 4096}
        (diagnostic / (str(self.observed["pid"])+"-"+str(self.observed["process_created"])+".jsonl")).write_text(
            (json.dumps(earlier)+"\n") * 96 + json.dumps(record)+"\n")
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": str(self.root / "log-root")}): run.collect_startup_log()
        self.module_mock.side_effect = lambda *unused: {**copy.deepcopy(self.module_observed), 'modules': []}
        with self.assertRaises(retest.protection.Refused): run.capture()
        self.assertEqual(run.state["capture_health"]["module_state"], "not_yet_observable")
        self.module_mock.side_effect = lambda *unused: copy.deepcopy(self.module_observed)
        with self.assertRaises(retest.protection.Refused): run.capture()
        run.export()
        report = self.report(run.directory)
        self.assertEqual(report["capture_health"]["module_state"], "verified")
        self.assertEqual([x["state"] for x in report["capture_health"]["module_history"]], ["not_yet_observable", "verified"])
        self.assertEqual(report["native_failure"]["fault"], "native_profile")
        self.assertEqual(report["native_failure"]["first_failed_stage"], "decode")
        self.assertEqual(report["native_failure"]["first_failure"]["native_value"], 4)
        summary = (run.directory / "shareable/SUMMARY.md").read_text()
        self.assertIn("Native game failure:", summary)
        self.assertIn("native_authentication_refused", summary)

    def test_module_roles_include_system_forwarder_reject_wrong_paths_and_duplicates(self):
        state, rows = retest.module_roles(self.config, self.manifest, self.module_observed)
        self.assertEqual(state, 'verified')
        self.assertEqual({row['role'] for row in rows}, {'game_core', 'game_proxy', 'windows_system_forwarder'})
        for defect in ('unknown_path', 'wrong_hash', 'duplicate_proxy', 'duplicate_core', 'duplicate_system'):
            with self.subTest(defect=defect):
                observed = copy.deepcopy(self.module_observed)
                if defect == 'unknown_path': observed['modules'][2]['path'] = str(self.root / 'untrusted/msimg32.dll')
                elif defect == 'wrong_hash': observed['modules'][1]['sha256'] = 'd' * 64
                else: observed['modules'].append(copy.deepcopy(observed['modules'][{'duplicate_proxy': 1, 'duplicate_core': 0, 'duplicate_system': 2}[defect]]))
                self.assertEqual(retest.module_roles(self.config, self.manifest, observed)[0], 'verified_mismatch')

    def test_optional_collection_timeout_recovers_identity_and_deduplicates_warning(self):
        run = retest.Run(self.config_path, 'RUN'); run.prepare(); run.state['process'] = copy.deepcopy(self.observed)
        run.state['process']['instance_id'] = 'b' * 32
        failure = retest.CollectionUnavailable('module_collection', {'timed_out': True, 'exit_code': None,
            'stderr': 'module_disk_hash\nDO_NOT_EXPORT', 'stdout': '', 'duration_ms': 8016})
        self.module_mock.side_effect = [failure, failure, failure, copy.deepcopy(self.module_observed)]
        with mock.patch.object(retest.time, 'sleep'):
            with self.assertRaises(retest.protection.Refused): run.capture()
        self.assertEqual(run.state['capture_health']['module_state'], 'collection_unavailable')
        self.assertEqual(run.state['process']['instance_id'], 'b' * 32)
        with self.assertRaises(retest.protection.Refused): run.capture()
        operation = run.state['collection_health']['operations']['module_collection']
        self.assertEqual((operation['failures'], operation['recoveries']), (3, 1))
        self.assertEqual(operation['last_failure']['operation'], 'module_disk_hash')
        self.assertEqual(run.state['capture_health']['module_state'], 'verified')
        for unused in range(3): run.fail(retest.Refused('same_warning'), 'safe_capture')
        self.assertEqual(run.state['primary_failure']['occurrences'], 3)
        self.assertEqual(run.state['secondary_failures'], [])
        run.export(); report = self.report(run.directory)
        self.assertNotIn('DO_NOT_EXPORT', json.dumps(report))
        self.assertNotIn(self.system_forwarder, json.dumps(report))
        self.assertEqual(report['process']['modules'][2]['role'], 'windows_system_forwarder')

    def test_process_collection_errors_preserve_identity_and_replacement_stops_queries(self):
        run = retest.Run(self.config_path, 'RUN'); run.prepare(); run.state['process'] = copy.deepcopy(self.observed)
        run.state['process']['instance_id'] = 'b' * 32
        original = copy.deepcopy(run.state['process'])
        for raw in ({'timed_out': True, 'exit_code': None}, {'timed_out': False, 'exit_code': 5, 'stderr': 'access denied'}):
            self.process_mock.side_effect = retest.CollectionUnavailable('process_identity', raw)
            with mock.patch.object(retest.time, 'sleep'): run.capture()
            self.assertEqual(run.state['process'], original)
            self.assertEqual(self.queries, [])
        self.process_mock.side_effect = lambda *unused: {**self.observed, 'process_created': '134077788800000001'}
        with self.assertRaisesRegex(retest.Refused, 'game_process_replaced'): run.capture()
        self.process_mock.side_effect = lambda *unused: copy.deepcopy(self.observed)
        self.query_responses['basic'] = lambda response, code: ({**response, 'instance_id': 'd' * 32}, code)
        with self.assertRaisesRegex(retest.Refused, 'core_instance_replaced'): run.capture()
        self.assertEqual(self.queries, ['basic'])

    def test_inventory_nonzero_and_cancel_are_distinct_from_process_exit(self):
        with mock.patch.object(retest, 'run_command', return_value={'exit_code': 5, 'stdout_truncated': False, 'cancelled': False}):
            with self.assertRaises(retest.CollectionUnavailable): self.actual_observe_game(self.config, self.root / 'observation')
        with mock.patch.object(retest, 'run_command', return_value={'cancelled': True}):
            with self.assertRaises(KeyboardInterrupt): self.actual_observe_game(self.config, self.root / 'cancel')

    def test_real_metadata_change_during_copy_refuses_full_run(self):
        helper = self.candidate / "prepare_vanilla_backup.py"
        text = helper.read_text()
        marker = '\nif __name__ == "__main__":'
        injection = "\nreal_copy = copy_file\ndef copy_file(source, destination):\n    result = real_copy(source, destination)\n    api = kernel()\n    handle = api.CreateFileW(" + repr(str(self.campaign)) + ", 0x100, 7, None, 3, 0, None)\n    if handle == ctypes.c_void_p(-1).value: raise RuntimeError('fixture metadata handle')\n    try:\n        ticks = ctypes.c_uint64(134077788800000000)\n        api.SetFileTime.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]\n        if not api.SetFileTime(handle, None, None, ctypes.byref(ticks)): raise RuntimeError('fixture SetFileTime')\n    finally: api.CloseHandle(handle)\n    return result\n"
        helper.write_text(text.replace(marker, injection + marker))
        self.update_member("prepare_vanilla_backup.py")
        code, output = self.stage("RUN")
        state, directory = self.state()
        self.assertEqual(code, 1, output)
        self.assertIn("source changed", output)
        self.assertIsNone(state["descriptor"])
        self.assertFalse(self.queries)
        self.assertEqual(self.report(directory)["admission_observation"], "NOT_TESTED")

    def test_operator_interruption_exports_and_next_shell_reuses_exact_reference(self):
        self.process_mock.side_effect = KeyboardInterrupt
        code, output = self.stage("RUN")
        state, directory = self.state()
        self.assertEqual(code, 1, output)
        self.assertIn("operator_interrupted", output)
        self.assertFalse((self.game / "sentinel-prelaunch.txt").exists())
        self.assertEqual(self.report(directory)["game_observation"], "NOT_OBSERVED")
        import subprocess
        command = [str(POWERSHELL), "-NoProfile", "-File", str(self.candidate / "Retest-MilestoneA.ps1"), "-Config", str(self.config_path), "-Stage", "PREPARE"]
        completed = subprocess.run(command, capture_output=True, text=True, cwd=self.root)
        self.assertEqual(completed.returncode, 0, completed.stdout+completed.stderr)
        newer, new_directory = self.state()
        self.assertNotEqual(directory, new_directory)
        self.assertEqual(newer["protection"]["reference_directory"], state["protection"]["reference_directory"])
        self.report(new_directory)

    def test_corrupt_reference_and_wrong_source_identity_refuse_before_activation(self):
        self.assertEqual(self.stage("PREPARE")[0], 0)
        backup = Path(self.config["OriginalBackupDirectory"])
        (backup / "local_provider/settings.cfg").write_bytes(b"corrupt fixture backup")
        code, output = self.stage("RUN")
        state, directory = self.state()
        self.assertEqual(code, 1, output)
        self.assertIn("backup", output)
        self.assertFalse(self.queries)
        self.assertEqual(self.report(directory)["admission_observation"], "NOT_TESTED")

    def test_export_recovers_exact_run_without_overwriting_first_report(self):
        self.assertEqual(self.stage("PREPARE")[0], 0)
        state, directory = self.state()
        original = (directory / (directory.name+"-shareable.zip")).read_bytes()
        code, output = self.stage("EXPORT")
        self.assertEqual(code, 0, output)
        self.assertEqual(original, (directory / (directory.name+"-shareable.zip")).read_bytes())
        self.assertEqual(len(list(directory.glob("*.zip"))), 2)

    def test_wrong_account_in_previous_state_cannot_choose_new_baseline(self):
        self.assertEqual(self.stage("PREPARE")[0], 0)
        self.config["SteamAccount32"] = "67890"
        retest.write_json(self.config_path, self.config)
        code, output = self.stage("PREPARE")
        self.assertEqual(code, 1, output)
        self.assertIn("prior_run_account_or_source_identity_conflict", output)
        state, directory = self.state()
        self.assertNotIn("protection", state)
        self.report(directory)

    def test_added_removed_and_automatic_record_privacy(self):
        self.assertEqual(self.stage("PREPARE")[0], 0)
        (self.local / "settings.cfg").unlink()
        (self.local / "replacement.cfg").write_bytes(b"fixture replacement")
        self.process_mock.side_effect = self.closed_after_capture
        diagnostic = self.root / "log-root/SentinelCore/diagnostics"
        diagnostic.mkdir(parents=True)
        record = {"schema": "sentinel-startup-v1", "pid": self.observed["pid"], "process_created": str(self.observed["process_created"]),
                  "build_id": self.manifest["build_id"], "admission": {"state": 4, "fault": 2},
                  "installation": {"phase": 3}, "private_payload": "DO_NOT_EXPORT"}
        (diagnostic / (str(self.observed["pid"])+"-"+str(self.observed["process_created"])+".jsonl")).write_text(json.dumps(record)+"\n")
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": str(self.root / "log-root")}): self.stage("RUN")
        state, directory = self.state()
        report = self.report(directory)
        self.assertEqual(report["preparation"]["historical_comparison"]["counts"]["added"], 1)
        self.assertEqual(report["preparation"]["historical_comparison"]["counts"]["removed"], 1)
        self.assertEqual(report["automatic_startup"]["state"], "captured")
        self.assertNotIn("private_payload", report["automatic_startup"]["records"][0])

    def test_short_lived_attempt_recovered_by_exact_control_hash_without_PID_guess(self):
        import time
        diagnostic = self.root / "log-root/SentinelCore/diagnostics"
        diagnostic.mkdir(parents=True)
        def already_closed(config, prefix):
            state, directory = self.state()
            created = int(time.time()*10000000)+116444736000000000
            record = {"schema": "sentinel-startup-v1", "pid": 45678, "process_created": str(created),
                "build_id": self.manifest["build_id"], "control_sha256": state["control_sha256"],
                "admission": {"state": 4, "fault": 2},
                "installation": {"phase": 3, "primary_failure": {"sequence": 1, "stage": 1, "reason": 2, "win32_error": 52}}}
            (diagnostic / ("45678-"+str(created)+".jsonl")).write_text(json.dumps(record)+"\n")
            raise retest.Refused("no_game_process")
        self.process_mock.side_effect = already_closed
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": str(self.root / "log-root")}): code, output = self.stage("RUN")
        state, directory = self.state()
        self.assertEqual(code, 1, output)
        self.assertFalse(self.queries)
        report = self.report(directory)
        self.assertEqual(report["game_observation"], "observed_in_automatic_record_only")
        self.assertEqual(report["hook_installation"], "native_failure_in_automatic_record")
        self.assertEqual(report["primary_failure"]["win32_error"], 52)
        self.assertEqual(report["comparison"]["response"]["result"], "vanilla_campaign_unchanged")

    def test_completed_export_preserves_outcome_after_later_legitimate_change(self):
        self.process_mock.side_effect = self.closed_after_capture
        self.stage("RUN")
        previous, directory = self.state()
        self.campaign.write_bytes(b"legitimate later fixture progress")
        self.stage("EXPORT")
        current, same_directory = self.state()
        self.assertEqual(current["comparison"], previous["comparison"])
        self.assertEqual(directory, same_directory)

    def test_recovered_previous_outcome_survives_multiple_later_runs(self):
        self.process_mock.side_effect = self.closed_after_capture
        self.stage("RUN")
        previous, directory = self.state()
        previous["comparison"] = {"state": "not_performed", "reason": "fixture_interruption"}
        retest.write_json(directory / "private/state.json", previous)
        self.assertEqual(self.stage("PREPARE")[0], 0)
        self.campaign.write_bytes(b"legitimate progress after completed recovery")
        code, output = self.stage("PREPARE")
        state, newest = self.state()
        self.assertEqual(code, 0, output)
        self.assertTrue(state.get("protection"))
        recovered = next(row for row in state["previous_comparisons"] if row["run_id"] == directory.name)
        self.assertEqual(recovered["retained_comparison"]["response"]["modified"], 0)


if __name__ == "__main__":
    unittest.main()
