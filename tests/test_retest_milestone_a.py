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


class StartupStreamingTests(unittest.TestCase):
    def diagnostic_run(self):
        temporary = tempfile.TemporaryDirectory(prefix='diagnostics-', dir=Path(__file__).parent)
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        run = object.__new__(retest.Run)
        run.directory = root
        (root / 'private').mkdir()
        run.state = {'process': {'pid': 123, 'process_created': '456'}, 'manifest': {'build_id': 'a' * 64},
                     'namespace_id': 'b' * 64}
        run.save = mock.Mock(); run.fail = mock.Mock()
        patch = mock.patch.dict(os.environ, {'LOCALAPPDATA': str(root)})
        patch.start(); self.addCleanup(patch.stop)
        path = root / 'SentinelCore/diagnostics/123-456.jsonl'
        path.parent.mkdir(parents=True)
        event = {'sequence': 301, 'at_ms': 801, 'operation': 9, 'thread': 2, 'stage': 'profile_output', 'status': 4,
                 'predicate': 'profile_selection_invalid', 'facts': {'native_result': -1, 'expected_slot': 0},
                 'private': {'source': '0x7FFFAABBCCDDEE'}}
        row = {'schema': 'sentinel-startup-v1', 'pid': 123, 'process_created': '456', 'build_id': 'a' * 64,
               'at_ms': 900, 'admission': {'namespace_id': 'b' * 64}, 'b_diagnostics': {
                   'sequence': 400, 'first_failure': event, 'stages': {'profile_output': event}}}
        return run, path, row

    def test_latest_failure_survives_both_history_caps(self):
        for padding, count in ((0, 130), (20000, 100)):
            with self.subTest(padding=padding):
                run, path, latest = self.diagnostic_run()
                prior = {**latest, 'at_ms': 1, 'b_diagnostics': {'sequence': 1}, 'padding': 'x' * padding}
                path.write_text((json.dumps(prior) + '\n') * count)
                retest.write_json(path.with_suffix('.latest.json'), latest)
                run.collect_startup_log()
                log = run.state['automatic_log']
                self.assertTrue(log['truncated'])
                self.assertLessEqual(len(log['records']), 128)
                self.assertEqual(log['latest_state'], 'captured')
                self.assertEqual(log['first_failure']['predicate'], 'profile_selection_invalid')
                self.assertEqual(log['sequence_gap'], 398)
                cause = retest.Run.campaign_failure(retest.automatic_rows(log), {'difficulty': 3})
                self.assertEqual(cause['timing'], 'native_b_event')
                self.assertEqual(cause['first_failure']['facts']['native_result'], -1)
                self.assertNotIn('0x7FFF', json.dumps(log))

    def test_latest_rejects_foreign_identity_and_stale_sequence_preserving_prior(self):
        run, path, row = self.diagnostic_run()
        retest.write_json(path.with_suffix('.latest.json'), row)
        run.collect_startup_log()
        retained = copy.deepcopy(run.state['automatic_log']['latest'])
        for key, value in (('pid', 124), ('process_created', '457'), ('build_id', 'c' * 64),
                           ('admission', {'namespace_id': 'c' * 64}), ('at_ms', 899),
                           ('b_diagnostics', {'sequence': 399})):
            with self.subTest(key=key):
                retest.write_json(path.with_suffix('.latest.json'), {**row, key: value})
                run.collect_startup_log()
                self.assertEqual(run.state['automatic_log']['latest_state'], 'rejected')
                self.assertEqual(run.state['automatic_log']['latest'], retained)
                self.assertEqual(run.state['automatic_log']['first_failure']['sequence'], 301)

    def test_latest_malformed_memory_and_io_degrade_without_erasing_first_cause(self):
        run, path, row = self.diagnostic_run()
        retest.write_json(path.with_suffix('.latest.json'), row)
        run.collect_startup_log()
        retained = copy.deepcopy(run.state['automatic_log']['latest'])
        for content in ('{', '[]', 'x' * 65537, json.dumps({**row, 'profile': None})):
            path.with_suffix('.latest.json').write_text(content)
            run.collect_startup_log()
            self.assertEqual(run.state['automatic_log']['latest_state'], 'rejected')
            self.assertEqual(run.state['automatic_log']['latest'], retained)
        for error in (MemoryError(), OSError('DO_NOT_EXPORT private path')):
            with mock.patch.object(retest, 'startup_latest', side_effect=error): run.collect_startup_log()
            self.assertEqual(run.state['automatic_log']['latest_state'], type(error).__name__)
            self.assertEqual(run.state['automatic_log']['latest'], retained)
            self.assertNotIn('DO_NOT_EXPORT', json.dumps(run.state))

    def test_b_diagnostic_sanitizer_rejects_nonprotocol_fields(self):
        run, path, row = self.diagnostic_run()
        event = row['b_diagnostics']['first_failure']
        event['facts'].update({'payload': 'DO_NOT_EXPORT', 'memory_address': 123456, 'bad/key': 8,
                               'overflow': 1 << 63, 'negative': -(1 << 63), 'boolean': True})
        event['private'] = {'source': 'DO_NOT_EXPORT'}
        safe = retest.safe_b_diagnostics(row['b_diagnostics'])
        self.assertEqual(safe['first_failure']['facts'], {'native_result': -1, 'expected_slot': 0, 'negative': -(1 << 63)})
        self.assertNotIn('DO_NOT_EXPORT', json.dumps(safe))
        event['predicate'] = 'private/path DO_NOT_EXPORT'
        self.assertEqual(retest.safe_b_diagnostics(row['b_diagnostics'])['first_failure']['predicate'], 'redacted_nonprotocol_predicate')

    def test_startup_gate_stage_preserves_relative_context(self):
        gate = {'sequence': 7, 'at_ms': 900, 'thread': 42, 'status': 3, 'stage': 'startup_gate',
                'predicate': 'startup_gate_root_released',
                'facts': {'install_begin_ms': 100, 'arm_ms': 200, 'startup_enter_ms': 300,
                          'first_wait_ms': 301, 'install_complete_ms': 600,
                          'startup_thread': 42, 'manager_present': 0, 'root_equals_binding': 1,
                          'init_slot_equals_target0': 1, 'caller_rva': 0x4323fc,
                          'image_base': 'PRIVATE_BASE', 'source_name': 'PRIVATE_SOURCE_NAME'},
                'source': 123456}
        safe = retest.safe_b_diagnostics({'stages': {'startup_gate': gate}})
        self.assertEqual(safe['stages']['startup_gate']['facts']['caller_rva'], 0x4323fc)
        self.assertEqual(safe['stages']['startup_gate']['facts']['startup_enter_ms'], 300)
        self.assertEqual(safe['stages']['startup_gate']['facts']['first_wait_ms'], 301)
        self.assertEqual(safe['stages']['startup_gate']['facts']['install_complete_ms'], 600)
        self.assertEqual(safe['stages']['startup_gate']['facts']['root_equals_binding'], 1)
        self.assertNotIn('PRIVATE', json.dumps(safe))
        self.assertNotIn('source', safe['stages']['startup_gate'])

    def test_b_failure_before_campaign_activation_stops_manual_progress(self):
        run, path, row = self.diagnostic_run()
        run.state['campaign_case'] = {'phase': 'create', 'options': {'difficulty': 3}}
        row['b_diagnostics']['first_failure']['stage'] = 'catalog'
        row['b_diagnostics']['first_failure']['predicate'] = 'catalog_selection_ambiguous'
        retest.write_json(path.with_suffix('.latest.json'), row)
        with contextlib.redirect_stdout(io.StringIO()): run.collect_startup_log()
        self.assertEqual(run.state['campaign_case']['failure']['reason'], 'catalog_selection_ambiguous')
        self.assertEqual(run.state['campaign_case']['runtime_proof'], 'failed_observing_until_normal_exit')
        run.fail.assert_called_once()

    def test_bounded_chunks_recover_after_oversized_and_invalid_records(self):
        content = b'{"n":1}\n' + b'x' * 150000 + b'\n\xff\n' + b'{"n":2}'
        class Bounded(io.BytesIO):
            def readline(self, size=-1):
                if not 0 < size <= 65536: raise AssertionError(size)
                return super().readline(size)
            def read(self, size=-1):
                if size != 1: raise AssertionError(size)
                return super().read(size)
        path = mock.Mock(); path.open.return_value = Bounded(content)
        status = {}
        self.assertEqual(list(retest.startup_records(path, status)), [{'n': 1}, {'n': 2}])
        self.assertTrue(status['truncated'])

    def test_total_limit_never_parses_the_last_record_prefix(self):
        path = mock.Mock(); path.open.return_value = io.BytesIO(b'{"n":1}\n{"n":22}\n')
        status = {}
        with mock.patch.object(retest, 'MAX_STARTUP_LOG', 12):
            self.assertEqual(list(retest.startup_records(path, status)), [{'n': 1}])
        self.assertTrue(status['truncated'])

    def test_collection_memory_or_io_failure_keeps_prior_evidence_and_process(self):
        for error in (MemoryError(), OSError('private path must not be exported')):
            run = object.__new__(retest.Run)
            run.state = {'process': {'pid': 123}, 'automatic_log': {'records': [{'at_ms': 7}]},
                         'primary_failure': {'distinction': 'native_profile'}}
            before = copy.deepcopy(run.state)
            run.save = mock.Mock()
            run._collect_startup_log = mock.Mock(side_effect=error)
            run.collect_startup_log()
            self.assertEqual(run.state, {**before, 'collection_degradation': type(error).__name__})
            run._discover_recorded_process = mock.Mock(side_effect=error)
            self.assertFalse(run.discover_recorded_process())
            self.assertEqual(run.state['process'], before['process'])


class InterruptedRecoveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='interrupted-recovery-', dir=Path(__file__).parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.old = self.root / ('retest-' + '1' * 32) / 'private/state.json'
        self.old.parent.mkdir(parents=True)
        self.reference = self.root / 'backup' / retest.protection.MANIFEST
        self.reference.parent.mkdir(); self.reference.write_bytes(b'fixture reference')
        self.config = {'EvidenceRoot': str(self.root), 'APRoot': str(self.root / 'ap'), 'SteamAppRoot': str(self.root / 'steam'),
                       'SteamAccount32': 123, 'LocalProviderRoot': str(self.root / 'local')}
        self.namespace = 'a' * 64
        self.state = {'run_id': self.old.parent.parent.name, 'finished': False, 'namespace_id': self.namespace,
            'manifest': {'build_id': 'b' * 64}, 'process': {'pid': 12, 'process_created': '34', 'instance_id': 'c' * 32},
            'campaign_case': {'phase': 'create', 'failure': {'reason': 'profile_failed'}, 'launches': []},
            'protection': {'reference_directory': str(self.reference.parent), 'reference_manifest_sha256': retest.sha(self.reference)}}
        retest.write_json(self.old, self.state)
        retest.write_json(self.old.parent / 'config.json', self.config)
        ap = Path(self.config['APRoot']) / self.namespace
        steam = Path(self.config['SteamAppRoot']) / 'remote' / ('ap-' + self.namespace[:40])
        ap.mkdir(parents=True); steam.mkdir(parents=True)
        files = [ap / name for name in ('campaign.contract', 'owner.lock', 'session.manifest')]
        files.append(steam / ('sentinel-owner-' + self.namespace + '.txt'))
        for path in files: path.write_bytes(b'fixture metadata')
        self.receipt = {'run_id': self.state['run_id'], 'build_id': 'b' * 64, 'process': self.state['process'],
            'namespace_id': self.namespace, 'reference_manifest_sha256': retest.sha(self.reference),
            'comparison': {'result': 'vanilla_campaign_unchanged', 'baseline_campaign_files': 30,
                'current_campaign_files': 30, 'added': 0, 'removed': 0, 'modified': 0}, 'comparison_details': {'differences': []},
            'retained_evidence_sha256': {str(path): retest.sha(path) for path in (self.old, self.old.parent / 'config.json', self.reference)},
            'namespace_files': [{'path': str(path), 'sha256': retest.sha(path), 'size': path.stat().st_size} for path in files],
            'classification': 'interrupted_failed_create_preserved', 'stopped_before_and_after': True,
            'normal_exit_observed': False, 'original_case_modified': False, 'source_case_finished': False, 'runtime_B_passed': False}
        self.recovery = self.root / 'recovery.private.json'
        patch = mock.patch.object(retest.protection, 'require_stopped')
        self.stopped = patch.start(); self.addCleanup(patch.stop)

    def authorize(self, receipt=None):
        retest.write_json(self.recovery, receipt or self.receipt)
        self.config['CorrectiveCase'] = {'run_id': self.state['run_id'], 'state_sha256': retest.sha(self.old),
            'recovery_file': str(self.recovery), 'recovery_sha256': retest.sha(self.recovery)}

    def test_exact_interrupted_recovery_authorizes_new_case_without_edit_or_exit_claim(self):
        original = self.old.read_bytes()
        self.authorize()
        result = retest.Run.corrective_case(self.config)
        self.assertEqual(result['outcome'], 'interrupted_failed_create_preserved_stopped_comparison_unchanged')
        self.assertEqual(self.old.read_bytes(), original)
        self.assertFalse(retest.read_json(self.old)['finished'])
        self.assertEqual(self.stopped.call_count, 2)

    def test_interrupted_recovery_requires_every_identity_and_comparison_predicate(self):
        for key, value in (('stopped_before_and_after', False), ('normal_exit_observed', True),
                           ('original_case_modified', True), ('source_case_finished', True), ('runtime_B_passed', True),
                           ('build_id', 'd' * 64), ('namespace_id', 'd' * 64), ('process', {'pid': 999}),
                           ('comparison', {**self.receipt['comparison'], 'modified': 1}),
                           ('comparison_details', {'differences': ['changed']})):
            with self.subTest(key=key):
                self.authorize({**self.receipt, key: value})
                with self.assertRaises(retest.Refused): retest.Run.corrective_case(self.config)

    def test_interrupted_recovery_rejects_changed_evidence_and_any_campaign_payload(self):
        self.authorize()
        retained = self.old.parent / 'config.json'
        original = retained.read_bytes(); retained.write_bytes(original + b' ')
        with self.assertRaisesRegex(retest.Refused, 'retained_evidence_changed'): retest.Run.corrective_case(self.config)
        retained.write_bytes(original)
        (Path(self.config['APRoot']) / self.namespace / 'checkpoint.bin').write_bytes(b'not metadata')
        with self.assertRaisesRegex(retest.Refused, 'namespace_payload_or_change'): retest.Run.corrective_case(self.config)


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

    def configure_campaign_fixture(self):
        self.manifest['milestone_b_options'] = {'provenance': 'synthetic-fixture', 'campaign': 'base', 'starting_stage': 'base_start', 'difficulty': 3}
        retest.write_json(self.candidate / 'manifest.json', self.manifest)
        def observed(config, prefix):
            if 'discovery-resume' in str(prefix):
                self.observed['pid'] = 45679; self.observed['process_created'] = '134077788800000001'
            return self.closed_after_capture(config, prefix)
        self.process_mock.side_effect = observed
        def admitted(response, code):
            response['instance_id'] = ('c' if self.observed['pid'] == 45679 else 'b') * 32
            if response['operation'] == 'save_admission':
                namespace = self.state()[0]['namespace_id']
                response.update(state='admitted', fault='none', prepared_routes=63, required_routes=63, namespace_id=namespace, native_root='ap-' + namespace[:40])
            if response['operation'] == 'save_installation': response['installation'] = {'phase': 'ready', 'primary_failure': None}
            return response, 0
        self.query_responses.update({query: admitted for query in retest.QUERIES})
        def automatic(run):
            resumed = run.state['campaign_case']['phase'] == 'resume'
            campaign = {'enabled': True, 'resumed': resumed, 'phase': 'reopened' if resumed else 'checkpoint_saved', 'reason': 'none',
                'difficulty': 3, 'effective_difficulty': 3, 'loaded_difficulty': 3 if resumed else 4, 'native_saved': not resumed, 'readback_verified': not resumed,
                'continuity_persisted': True, 'native_factory_matched': not resumed, 'source_verified': resumed, 'parser_completed': resumed, 'map_active': True,
                'checkpoint': 1, 'source_checkpoint': 1 if resumed else 0, 'map': 'game/sp/initial'}
            run.state['automatic_log'] = {'state': 'recorded', 'records': [{'campaign': campaign, 'pid': run.state['process']['pid'],
                'process_created': run.state['process']['process_created'], 'build_id': run.state['manifest']['build_id'],
                'admission': {'state': 3, 'fault': 0, 'flags': 7, 'prepared_routes': 63, 'required_routes': 63, 'namespace_id': run.state['namespace_id']}}]}
        return automatic

    def test_campaign_two_launches_keep_options_identity_and_compare_each_reference(self):
        with mock.patch.object(retest.Run, 'collect_startup_log', self.configure_campaign_fixture()):
            code, output = self.stage('RUN', '--scenario', 'B')
        state, directory = self.state()
        self.assertEqual(code, 0, output)
        case = state['campaign_case']; self.assertEqual(case['phase'], 'completed')
        self.assertEqual([v['phase'] for v in case['launches']], ['create', 'resume'])
        self.assertTrue(all(v['comparison']['response']['result'] == 'vanilla_campaign_unchanged' for v in case['launches']))
        descriptors = [Path(case['descriptors'][phase]['path']).read_text() for phase in ('create', 'resume')]
        self.assertEqual(descriptors[0].replace('intent=create', 'intent=resume'), descriptors[1])
        self.assertIn('difficulty=3\n', descriptors[0])
        self.assertNotEqual(case['launches'][0]['process'], case['launches'][1]['process'])
        self.assertFalse((self.game / 'sentinel-prelaunch.txt').exists())
        self.assertEqual(len(list(directory.glob('*-shareable.zip'))), 1)
        report = self.report(directory)
        self.assertEqual(report['campaign_case']['phase'], 'completed')
        self.assertIn('backup_verified', report['campaign_case'])

    def test_campaign_interruption_continues_exact_case_without_second_creation(self):
        automatic = self.configure_campaign_fixture()
        complete = retest.Run.complete_campaign_launch
        def interrupted(run):
            complete(run)
            if run.state['campaign_case']['phase'] == 'prepare_resume': raise KeyboardInterrupt()
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic), mock.patch.object(retest.Run, 'complete_campaign_launch', interrupted):
            code, output = self.stage('RUN', '--scenario', 'B')
        first, directory = self.state()
        self.assertNotEqual(code, 0, output)
        self.assertEqual(first['campaign_case']['phase'], 'prepare_resume')
        identity = first['campaign_case']['generation_fingerprint']
        first_evidence = (directory / 'private/campaign-create.private.json').read_bytes()
        blocked, _ = self.stage('RUN', '--scenario', 'B')
        self.assertNotEqual(blocked, 0)
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic):
            _, output = self.stage('RUN', '--scenario', 'B', '--resume-case', first['run_id'])
        current, current_directory = self.state()
        self.assertEqual(current_directory, directory)
        self.assertEqual(current['campaign_case']['phase'], 'completed', output)
        self.assertEqual(current['campaign_case']['generation_fingerprint'], identity)
        self.assertEqual((directory / 'private/campaign-create.private.json').read_bytes(), first_evidence)
        self.assertEqual(len(current['campaign_case']['launches']), 2)
        self.assertEqual(current['captures'], 2)

    def test_campaign_native_root_shutdown_completes_both_launches(self):
        automatic = self.configure_campaign_fixture()
        def shutdown(run):
            automatic(run)
            row = run.state['automatic_log']['records'][0]
            row['admission']['flags'] = 5
            row['b_diagnostics'] = {'stages': {'session': {'sequence': 229, 'status': 3,
                'predicate': 'native_root_shutdown_provider_cleanup', 'facts': {'shutdown_rva': 0x675300}}}}
        with mock.patch.object(retest.Run, 'collect_startup_log', shutdown):
            code, output = self.stage('RUN', '--scenario', 'B')
        self.assertEqual(code, 0, output)
        self.assertEqual(self.state()[0]['campaign_case']['phase'], 'completed')

    def test_campaign_terminal_admission_requires_exact_shutdown_and_no_fault(self):
        import copy
        row = {'admission': {'state': 3, 'fault': 0, 'flags': 5, 'prepared_routes': 63,
            'required_routes': 63, 'namespace_id': 'a'*64},
            'b_diagnostics': {'stages': {'session': {'sequence': 229, 'status': 3,
                'predicate': 'native_root_shutdown_provider_cleanup', 'facts': {'shutdown_rva': 0x675300}}}}}
        for field, value in (('fault', 7), ('state', 5), ('flags', 1), ('namespace_id', 'b'*64)):
            invalid = copy.deepcopy(row); invalid['admission'][field] = value
            self.assertFalse(retest.Run.campaign_admission_after_close([invalid], 'a'*64))
        for field, value in (('predicate', 'session_requests_stopped'), ('status', 4), ('sequence', 0),
                             ('facts', {'shutdown_rva': 0x675d24})):
            invalid = copy.deepcopy(row); invalid['b_diagnostics']['stages']['session'][field] = value
            self.assertFalse(retest.Run.campaign_admission_after_close([invalid], 'a'*64))

    def test_campaign_failure_keeps_observing_through_transient_log_error_and_normal_close(self):
        automatic = self.configure_campaign_fixture()
        calls = []
        def failed(run):
            calls.append(run.state['campaign_case']['phase'])
            if len(calls) == 2: raise OSError('temporary log sharing failure')
            automatic(run)
            row = run.state['automatic_log']['records'][0]
            row['at_ms'] = 1000 + len(calls)
            row['campaign'].update(reason='native_transition_return_failed', phase='refused', failure_at_ms=900,
                native_saved=False, continuity_persisted=False, map_active=False)
            row['profile'] = {'request_id': 1, 'first_failed_stage': 'write_after_refusal', 'first_failure': {'first_ms': 950, 'changed_ms': 950}}
        observations = []
        def alive_then_close(config, prefix):
            if 'observation-' in str(prefix):
                observations.append(str(prefix))
                self.assertTrue((self.game / 'sentinel-prelaunch.txt').exists())
                if len(observations) == 3: raise retest.Refused('no_game_process')
            return copy.deepcopy(self.observed)
        self.process_mock.side_effect = alive_then_close
        with mock.patch.object(retest.Run, 'collect_startup_log', failed):
            code, output = self.stage('RUN', '--scenario', 'B')
        state, directory = self.state()
        self.assertNotEqual(code, 0, output)
        self.assertEqual(len(observations), 3)
        self.assertEqual(output.count('Teste B falhou.'), 1)
        self.assertTrue(all(phase == 'create' for phase in calls))
        self.assertEqual(state['campaign_case']['runtime_proof'], 'failed_closed_and_compared')
        self.assertEqual(state['comparison']['response']['result'], 'vanilla_campaign_unchanged')
        self.assertFalse((self.game / 'sentinel-prelaunch.txt').exists())
        report = self.report(directory)
        self.assertEqual(report['native_failure']['fault'], 'native_campaign')
        self.assertEqual(report['native_failure']['ordering'], 'campaign_before_profile')
        self.assertEqual(report['campaign_case']['failure']['at_ms'], 900)

    def failed_exit(self, interrupt):
        automatic = self.configure_campaign_fixture()
        self.config['ObservationSeconds'] = 0
        retest.write_json(self.config_path, self.config)
        alive = False
        def stopped():
            if alive: raise retest.protection.Refused('fixture process still alive')
        def process(config, prefix):
            nonlocal alive
            if 'observation-' in str(prefix):
                alive = True
                if interrupt: raise KeyboardInterrupt()
            return copy.deepcopy(self.observed)
        def failed(run):
            automatic(run)
            run.state['automatic_log']['records'][0]['campaign'].update(reason='native_transition_return_failed', failure_at_ms=900)
        self.process_mock.side_effect = process
        with mock.patch.object(retest.Run, 'collect_startup_log', failed), mock.patch.object(retest.protection, 'require_stopped', stopped):
            code, output = self.stage('RUN', '--scenario', 'B')
        state, directory = self.state()
        self.assertNotEqual(code, 0, output)
        self.assertEqual(state['campaign_case']['phase'], 'create')
        self.assertEqual(state['comparison']['state'], 'not_performed')
        self.assertFalse((self.game / 'sentinel-prelaunch.txt').exists())
        self.assertEqual(len(list(directory.glob('*-shareable.zip'))), 1)
        expected = 'operator_interrupted_partial_evidence' if interrupt else 'game_close_deadline_process_not_stopped'
        self.assertIn(expected, output)

    def test_failed_campaign_interruption_exports_partial_without_reactivation(self):
        self.failed_exit(True)

    def test_failed_campaign_deadline_exports_partial_without_second_launch(self):
        self.failed_exit(False)

    def test_prior_profile_event_remains_causal_before_campaign(self):
        automatic = self.configure_campaign_fixture()
        def failed(run):
            automatic(run)
            row = run.state['automatic_log']['records'][0]
            row['campaign'].update(reason='native_transition_return_failed', failure_at_ms=900)
            row['profile'] = {'request_id': 1, 'first_failed_stage': 'decode', 'first_failure': {'first_ms': 800, 'changed_ms': 800}}
        with mock.patch.object(retest.Run, 'collect_startup_log', failed):
            code, output = self.stage('RUN', '--scenario', 'B')
        _, directory = self.state()
        self.assertNotEqual(code, 0, output)
        self.assertEqual(self.report(directory)['native_failure']['ordering'], 'profile_before_campaign')
        self.assertEqual(self.report(directory)['native_failure']['fault'], 'native_profile')

    def corrective_case_fixture(self):
        automatic = self.configure_campaign_fixture()
        def failed(run):
            automatic(run)
            run.state['automatic_log']['records'][0]['campaign']['reason'] = 'native_transition_return_failed'
        with mock.patch.object(retest.Run, 'collect_startup_log', failed):
            code, _ = self.stage('RUN', '--scenario', 'B')
        self.assertNotEqual(code, 0)
        old, directory = self.state()
        old_path = directory / 'private/state.json'
        original = old_path.read_bytes()
        recovery = self.root / 'exact-failed-case-recovery.json'
        retest.write_json(recovery, {'run_id': old['run_id'], 'comparison': old['comparison']['response'],
            'reference_manifest_sha256': old['protection']['reference_manifest_sha256'],
            'retained_evidence_sha256': {str(old_path): retest.sha(old_path)}})
        self.config['CorrectiveCase'] = {'run_id': old['run_id'], 'state_sha256': retest.sha(old_path),
            'recovery_file': str(recovery), 'recovery_sha256': retest.sha(recovery)}
        retest.write_json(self.config_path, self.config)
        return automatic, old, directory, old_path, original

    def test_explicit_corrective_case_preserves_failed_case_and_allows_new_two_launches(self):
        automatic, old, directory, old_path, original = self.corrective_case_fixture()
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic):
            code, output = self.stage('RUN', '--scenario', 'B')
        current, current_dir = self.state()
        self.assertEqual(code, 0, output)
        self.assertNotEqual(current_dir, directory)
        self.assertEqual(old_path.read_bytes(), original)
        self.assertEqual(current['corrects_case']['run_id'], old['run_id'])
        self.assertNotEqual(current['campaign_case']['generation_fingerprint'], old['campaign_case']['generation_fingerprint'])
        self.assertEqual(current['campaign_case']['phase'], 'completed')

    def test_explicit_corrective_case_prepare_keeps_campaign_identity_for_ordinary_run(self):
        automatic, old, directory, old_path, original = self.corrective_case_fixture()
        self.assertEqual(retest.Run.campaign_lifecycle(old, directory)[0], 'ambiguous_or_interrupted_create')
        code, output = self.stage('PREPARE', '--scenario', 'B')
        self.assertEqual(code, 0, output)
        prepared, prepared_dir = self.state()
        active = retest.read_json(self.config['ActiveRun'])
        self.assertEqual(active['run_id'], prepared['run_id'])
        self.assertEqual(active['campaign_reference']['run_id'], old['run_id'])
        self.assertEqual(prepared['corrects_case']['run_id'], old['run_id'])
        self.assertEqual(prepared['preparation']['state'], 'protected')
        self.assertFalse(prepared.get('campaign_case'))
        self.assertFalse(prepared.get('descriptor'))
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic):
            code, output = self.stage('RUN', '--scenario', 'B')
        current, current_dir = self.state()
        self.assertEqual(code, 0, output)
        self.assertNotIn(current_dir, (directory, prepared_dir))
        self.assertEqual(current['corrects_case']['run_id'], old['run_id'])
        self.assertNotEqual(current['campaign_case']['generation_fingerprint'], old['campaign_case']['generation_fingerprint'])
        self.assertEqual(current['campaign_case']['phase'], 'completed')
        self.assertEqual(old_path.read_bytes(), original)

    def test_startup_ready_failure_precedes_downstream_parser_and_retires_case(self):
        automatic = self.configure_campaign_fixture()
        def failed(run):
            automatic(run)
            row = run.state['automatic_log']['records'][0]
            row.update(at_ms=18433390, profile={'request_id': 0, 'first_failed_stage': 'none'})
            row['campaign'].update(phase='armed', operation=0, checkpoint=0, native_saved=False,
                readback_verified=False, continuity_persisted=False, native_factory_matched=False)
            row['admission'].update(state=4, fault=6, flags=0)
            row['installation'] = {'phase': 2, 'last_completed_stage': 16, 'validated': 58, 'created': 40, 'enabled': 40,
                'primary_failure': {'stage': 19, 'reason': 13, 'at_ms': 18433375}}
            later = copy.deepcopy(row); later['at_ms'] = 18458515
            later['campaign'].update(phase='refused', reason='load_parser_source_not_correlated', failure_at_ms=18458500)
            run.state['automatic_log']['records'].append(later)
        with mock.patch.object(retest.Run, 'collect_startup_log', failed):
            code, output = self.stage('RUN', '--scenario', 'B')
        state, directory = self.state(); original = (directory / 'private/state.json').read_bytes()
        self.assertNotEqual(code, 0, output)
        self.assertEqual(output.count('Teste B falhou.'), 1)
        report = self.report(directory)
        self.assertEqual(report['hook_installation'], 'READY/PASS')
        self.assertEqual(report['startup_observation'], 'FAILED/unobserved')
        self.assertEqual(report['native_failure']['first_failure']['at_ms'], 18433375)
        self.assertEqual(report['native_failure']['ordering'], 'startup_before_profile_and_campaign_parser')
        self.assertEqual(report['campaign_case']['lifecycle'], 'terminal_failed_before_checkpoint')
        self.assertEqual(report['campaign_case']['failure']['fault'], 'native_startup')
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic):
            code, output = self.stage('RUN', '--scenario', 'B')
        current, _ = self.state()
        self.assertEqual(code, 0, output)
        self.assertEqual(current['corrects_case']['run_id'], state['run_id'])
        self.assertNotEqual(current['namespace_id'], state['namespace_id'])
        self.assertEqual((directory / 'private/state.json').read_bytes(), original)

    def test_profile_after_creation_terminal_export_prepare_and_ordinary_new_run(self):
        automatic = self.configure_campaign_fixture()
        def failed(run):
            automatic(run)
            row = run.state['automatic_log']['records'][0]
            row['at_ms'] = 1200
            row['campaign'].update(phase='native_created', checkpoint=0, operation=0, native_saved=False,
                readback_verified=False, continuity_persisted=False, native_factory_matched=False)
            row['admission'].update(state=5, fault=15)
            row['profile'] = {'request_id': 1, 'first_failed_stage': 'overlay',
                'first_failure': {'first_ms': 100, 'changed_ms': 1100, 'predicate': 'serializer_baseline_unavailable'}}
            boot = copy.deepcopy(row)
            boot['campaign']['enabled'] = False
            boot['admission'].update(state=0, fault=0, namespace_id='')
            boot['profile'] = {}
            run.state['automatic_log']['records'].insert(0, boot)
        with mock.patch.object(retest.Run, 'collect_startup_log', failed):
            code, output = self.stage('RUN', '--scenario', 'B')
        old, directory = self.state(); original = (directory / 'private/state.json').read_bytes()
        self.assertNotEqual(code, 0, output)
        self.assertEqual(output.count('Teste B falhou.'), 1)
        report = self.report(directory)
        self.assertEqual(report['native_failure']['ordering'], 'profile_refusal_after_native_creation')
        self.assertEqual(report['campaign_case']['failure']['at_ms'], 1100)
        self.assertEqual(report['campaign_case']['lifecycle'], 'terminal_failed_before_checkpoint')
        self.assertEqual(retest.read_json(self.config['ActiveRun'])['retired_campaign']['run_id'], old['run_id'])
        for change in ('checkpoint', 'identity', 'comparison', 'closed', 'not_faulted', 'truncated'):
            bad = copy.deepcopy(old)
            if change == 'checkpoint': bad['automatic_log']['records'][-1]['campaign']['checkpoint'] = 1
            if change == 'identity': bad['automatic_log']['records'][0]['pid'] += 1
            if change == 'comparison': bad['comparison']['response']['modified'] = 1
            if change == 'closed': bad['stages'][0]['failure'] = 'operator_interrupted_partial_evidence'
            if change == 'not_faulted': bad['automatic_log']['records'][-1]['admission']['state'] = 3
            if change == 'truncated': bad['automatic_log']['truncated'] = True
            self.assertEqual(retest.Run.campaign_lifecycle(bad, directory)[0], 'ambiguous_or_interrupted_create')
        self.assertNotEqual(self.stage('RUN', '--scenario', 'B', '--resume-case', old['run_id'])[0], 0)
        code, output = self.stage('PREPARE', '--scenario', 'B')
        self.assertEqual(code, 0, output)
        self.assertEqual(retest.read_json(self.config['ActiveRun'])['campaign_reference']['run_id'], old['run_id'])
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic):
            code, output = self.stage('RUN', '--scenario', 'B')
        current, _ = self.state()
        self.assertEqual(code, 0, output)
        self.assertEqual(current['corrects_case']['run_id'], old['run_id'])
        self.assertNotEqual(current['campaign_case']['generation_fingerprint'], old['campaign_case']['generation_fingerprint'])
        self.assertEqual((directory / 'private/state.json').read_bytes(), original)

    def test_prepare_keeps_ambiguous_campaign_guard(self):
        automatic = self.configure_campaign_fixture()
        with mock.patch.object(retest.Run, 'collect_startup_log', automatic), mock.patch.object(retest.Run, 'complete_campaign_launch', side_effect=KeyboardInterrupt):
            self.stage('RUN', '--scenario', 'B')
        old, directory = self.state(); before = (directory / 'private/state.json').read_bytes()
        self.assertEqual(self.stage('PREPARE', '--scenario', 'B')[0], 0)
        code, output = self.stage('RUN', '--scenario', 'B')
        self.assertNotEqual(code, 0)
        self.assertIn('ResumeCase_' + old['run_id'], output)
        self.assertEqual((directory / 'private/state.json').read_bytes(), before)

    def test_overlay_start_is_not_refusal_time(self):
        rows = [{'at_ms': 1200, 'campaign': {'enabled': True, 'reason': 'native_transition_return_failed', 'difficulty': 3, 'failure_at_ms': 900},
            'admission': {'fault': 15}, 'profile': {'first_failed_stage': 'overlay', 'first_failure': {'first_ms': 100, 'changed_ms': 1100}}}]
        failure = retest.Run.campaign_failure(rows, {'difficulty': 3})
        self.assertEqual(failure['fault'], 'native_campaign')
        self.assertEqual(failure['at_ms'], 900)

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

    def test_run_null_path_retains_alive_identity_then_compares_confirmed_exit(self):
        observations = iter([{**self.observed, 'path': None}] * 3 + [self.observed, None])
        def command(argv, prefix, timeout):
            if argv[-1] == retest.PROCESS_SCRIPT:
                value = next(observations) if 'observation-' in prefix.name else self.observed
                return {'exit_code': 0, 'cancelled': False, 'stdout_truncated': False,
                        'stdout': json.dumps([value] if value else []), 'stderr': ''}
            return self.fixture_command(argv, prefix, timeout)
        self.process_mock.side_effect = self.actual_observe_game
        with mock.patch.object(retest, 'run_command', side_effect=command), mock.patch.object(retest.time, 'sleep'):
            code, output = self.stage('RUN')
        state, directory = self.state()
        self.assertEqual(code, 1)  # The unrelated native refusal remains visible.
        self.assertEqual(state['process']['path'], self.observed['path'], output)
        health = state['collection_health']['operations']['process_identity']
        self.assertEqual(health['failures'], 3)
        self.assertTrue(health['last_failure']['original_process_alive'])
        self.assertEqual(health['recoveries'], 1)
        self.assertEqual(self.report(directory)['comparison']['response']['result'], 'vanilla_campaign_unchanged')
        changed = {**self.observed, 'path': None, 'process_created': '134077788800000001'}
        run = retest.Run(self.config_path, 'EXPORT')
        with mock.patch.object(retest, 'run_command', return_value={'exit_code': 0, 'cancelled': False,
             'stdout_truncated': False, 'stdout': json.dumps([changed])}):
            with self.assertRaisesRegex(retest.Refused, 'replaced_while_path_unavailable'):
                run.observe_identity(directory / 'private/null-replaced')

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

    def test_latest_b_failure_beyond_history_limit_is_precise_and_shareable(self):
        run = retest.Run(self.config_path, 'RUN')
        run.prepare()
        run.state['process'] = copy.deepcopy(self.observed)
        run.state['campaign_case'] = {'phase': 'create', 'options': {'difficulty': 3}, 'launches': [], 'runtime_proof': 'pending'}
        root = self.root / 'latest-log/SentinelCore/diagnostics'
        root.mkdir(parents=True)
        path = root / (str(self.observed['pid']) + '-' + self.observed['process_created'] + '.jsonl')
        event = {'sequence': 200, 'at_ms': 800, 'operation': 8, 'thread': 99, 'stage': 'profile_prepare', 'status': 4,
            'predicate': 'prepared_profile_payload_invalid', 'facts': {'files': 2, 'native_result': -9},
            'private': {'source': 'DO_NOT_EXPORT'}}
        row = {'schema': 'sentinel-startup-v1', 'pid': self.observed['pid'], 'process_created': self.observed['process_created'],
            'build_id': self.manifest['build_id'], 'admission': {'state': 5, 'fault': 15, 'namespace_id': run.state['namespace_id']},
            'at_ms': 900, 'campaign': {'enabled': True, 'difficulty': 3, 'reason': 'none', 'phase': 'native_created'},
            'b_diagnostics': {'sequence': 250, 'first_failure': event, 'stages': {'profile_prepare': event}}}
        prior = {**row, 'at_ms': 100, 'b_diagnostics': {}, 'campaign': {}, 'private_payload': 'DO_NOT_EXPORT' * 1500}
        path.write_text((json.dumps(prior) + '\n') * 150)
        retest.write_json(path.with_suffix('.latest.json'), row)
        with mock.patch.dict(os.environ, {'LOCALAPPDATA': str(self.root / 'latest-log')}): run.collect_startup_log()
        run.export()
        report = self.report(run.directory)
        self.assertEqual(report['native_failure']['first_failed_stage'], 'profile_prepare')
        self.assertEqual(report['native_failure']['first_failure']['predicate'], 'prepared_profile_payload_invalid')
        self.assertEqual(report['native_failure']['first_failure']['facts']['native_result'], -9)
        self.assertEqual(report['automatic_startup']['latest_state'], 'captured')
        self.assertTrue(report['automatic_startup']['truncated'])
        self.assertIn('prepared_profile_payload_invalid', (run.directory / 'shareable/SUMMARY.md').read_text())

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
                  "installation": {"phase": 3}, "private_payload": "DO_NOT_EXPORT",
                  "startup_route": {"adapter": "presence_query", "caller_class": "other_or_unretained",
                      "caller_rva": 0x148e225, "source_name": "PRIVATE_SOURCE_NAME", "image_base": "PRIVATE_BASE",
                      "source_kind": 2, "source_length": 7, "source_step": 5, "source_read_reason": 0, "source_read_error": 0,
                      "stack_count": 2, "stack_rva_0": 0x148e225, "stack_rva_1": 0x1499160, "stack_rva_8": "PRIVATE_STACK",
                      "private": {"caller": "PRIVATE_CALLER", "native_user": "PRIVATE_USER", "source_name_hex": "PRIVATE_HEX"}},
                  "campaign": {"parser_trace": {"source": "foreign_or_unowned_campaign",
                      "private": {"data": "PRIVATE_DATA", "directory_hex": "PRIVATE_DIRECTORY"}}}}
        (diagnostic / (str(self.observed["pid"])+"-"+str(self.observed["process_created"])+".jsonl")).write_text(json.dumps(record)+"\n")
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": str(self.root / "log-root")}): self.stage("RUN")
        state, directory = self.state()
        report = self.report(directory)
        self.assertEqual(report["preparation"]["historical_comparison"]["counts"]["added"], 1)
        self.assertEqual(report["preparation"]["historical_comparison"]["counts"]["removed"], 1)
        self.assertEqual(report["automatic_startup"]["state"], "captured")
        self.assertNotIn("private_payload", report["automatic_startup"]["records"][0])
        retained = report["automatic_startup"]["records"][0]
        self.assertEqual(retained["startup_route"]["adapter"], "presence_query")
        self.assertEqual(retained["startup_route"]["caller_rva"], 0x148e225)
        self.assertEqual(retained["startup_route"]["source_kind"], 2)
        self.assertEqual(retained["startup_route"]["source_step"], 5)
        self.assertEqual(retained["startup_route"]["stack_rva_1"], 0x1499160)
        self.assertEqual(retained["campaign"]["parser_trace"]["source"], "foreign_or_unowned_campaign")
        self.assertNotIn("private", retained["startup_route"])
        self.assertNotIn("private", retained["campaign"]["parser_trace"])
        for private in ("PRIVATE_CALLER", "PRIVATE_USER", "PRIVATE_DATA", "PRIVATE_DIRECTORY", "PRIVATE_BASE", "PRIVATE_SOURCE_NAME", "PRIVATE_HEX", "PRIVATE_STACK"):
            self.assertNotIn(private, json.dumps(report))

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
