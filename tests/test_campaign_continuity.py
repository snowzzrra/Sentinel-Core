"""Production campaign/storage/SDK tracking with synthetic native boundaries.

Each stage is a separate Windows host process. Only TemporaryDirectory fixtures
are touched; the target is sentinel_campaign_tests, never DOOM or Steam.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

EXE = Path(__file__).resolve().parents[1] / 'build/bin/sentinel_campaign_tests.exe'


@unittest.skipUnless(os.name == 'nt', 'Windows native contracts')
class CampaignContinuity(unittest.TestCase):
    def test_startup_refusal_keeps_downstream_parser_closed(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-startup-parser-') as root:
            self.stage('create', root, 3, 'startup_parser')

    def stage(self, mode, root, difficulty, defect=None):
        command = [str(EXE), mode, str(root), str(difficulty)]
        if defect: command.append(defect)
        result = subprocess.run(command, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        return result.stdout.decode()

    def test_four_immutable_difficulties_create_save_new_process_reopen(self):
        for difficulty in range(4):
            with self.subTest(difficulty=difficulty), tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
                created = self.stage('create', root, difficulty)
                resumed = self.stage('resume', root, difficulty)
                self.assertIn('native checkpoint correlation', created)
                self.assertIn('separate-process', resumed)
                self.assertNotEqual(created.split('pid=')[1], resumed.split('pid=')[1])
                self.stage('resume', root, (difficulty + 1) % 4, 'refuse_configuration')
                self.stage('resume', root, difficulty, 'wrong_difficulty')
                self.stage('resume', root, difficulty, 'missing_difficulty')
                self.stage('resume', root, difficulty, 'wrong_source')
                self.stage('resume', root, difficulty, 'failed_parser')
                self.stage('resume', root, difficulty, 'wrong_map')

    def test_native_transition_and_initial_writer_boundaries(self):
        for defect in ("native_return", "abnormal", "state_read", "generation", "difficulty", "nested", "initial_save", "initial_save_return_failed", "pending_transition", "queued_checkpoint", "menu_pending_save", "queued_foreign", "terminal_cleanup", "partial_save", "save_failure", "unrelated", "extra_life", "ultra"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix="sentinel-boundary-") as root:
                self.stage("create", root, 3, defect)
                if defect in ("nested", "initial_save", "pending_transition", "queued_checkpoint", "menu_pending_save"):
                    self.stage("resume", root, 3)
                elif defect in ("partial_save", "save_failure", "initial_save_return_failed", "queued_foreign"):
                    self.stage("resume", root, 3, "refuse_configuration")

    def test_pending_native_save_never_reopens_or_becomes_new(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 2, 'pending')
            self.stage('resume', root, 2, 'refuse_configuration')
            self.stage('create', root, 2, 'refuse_configuration')

    def test_profile_new_game_cutscene_checkpoint_menu_and_reopen_save(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-profile-lifecycle-') as root:
            created = self.stage('create', root, 3, 'profile_lifecycle')
            self.assertIn('checkpoint=2 profile_writes=4', created)
            checkpoint, = Path(root).rglob('campaign.checkpoint')
            second = checkpoint.read_text()
            self.assertIn('checkpoint=2\n', second)
            self.assertIn('state=native_saved_readback_verified\n', second)
            resumed = self.stage('resume', root, 3, 'profile_lifecycle')
            self.assertIn('separate-process checkpoint=3 source_checkpoint=2 profile_writes=2', resumed)
            self.assertNotEqual(created.split('pid=')[1], resumed.split('pid=')[1])
            third = checkpoint.read_text()
            self.assertIn('checkpoint=3\n', third)
            self.assertNotEqual(second, third)
            self.stage('resume', root, 2, 'refuse_configuration')

    def test_dirty_process_refuses_before_reservation(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 1, 'dirty')
            self.assertFalse(list(Path(root).rglob('campaign.contract')))

    def test_native_menu_reads_complete_primary_or_backup_then_continue_and_save(self):
        for selected in ('native_read', 'native_read_backup'):
            with self.subTest(selected=selected), tempfile.TemporaryDirectory(prefix='sentinel-native-read-') as root:
                self.stage('create', root, 3, 'native_read')
                checkpoint, = Path(root).rglob('campaign.checkpoint')
                self.assertIn('files=4\n', checkpoint.read_text())
                result = self.stage('resume', root, 3, selected)
                self.assertIn('menu/read/parser/Continue/save checkpoint=2 files=4', result)
                self.assertIn('checkpoint=2\n', checkpoint.read_text())

    def test_native_menu_and_continue_refuse_missing_duplicate_mixed_failed_or_uncorrelated_reads(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-native-read-refusal-') as root:
            self.stage('create', root, 3, 'native_read')
            checkpoint, = Path(root).rglob('campaign.checkpoint')
            original = checkpoint.read_bytes()
            for defect in ('missing', 'duplicate', 'mixed', 'failed', 'wrong_mode', 'wrong_caller', 'hash'):
                with self.subTest(defect=defect):
                    self.stage('resume', root, 3, 'native_read_' + defect)
                    self.assertEqual(checkpoint.read_bytes(), original)

    def test_unassociated_write_refuses_before_native_payload_mutation(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 2, 'unassociated')
            self.assertFalse(list(Path(root).rglob('campaign.checkpoint')))

    def test_incomplete_and_changed_metadata_refuse(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 3)
            checkpoint, = Path(root).rglob('campaign.checkpoint')
            original = checkpoint.read_bytes()
            checkpoint.write_bytes(original[:-1])
            self.stage('resume', root, 3, 'refuse_configuration')

    def test_v2_descriptor_requires_explicit_supported_immutable_options(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-descriptor-') as root:
            path = Path(root) / 'input.txt'
            valid = ('sentinel-test-session-v2\nseed_hex=74657374\nteam=0\nslot=1\ngeneration_fingerprint=' + 'b' * 64 +
                '\nprovenance=synthetic-fixture\nroot=' + root + '\ncampaign=base\nstarting_stage=base_start\ndifficulty=3\nintent=create\n')
            for invalid in (valid.replace('difficulty=3\n', ''), valid.replace('difficulty=3', 'difficulty=4'),
                    valid.replace('difficulty=3', 'difficulty=-1'), valid.replace('intent=create', 'intent=guess'),
                    valid.replace('campaign=base', 'campaign=tag1')):
                path.write_text(invalid, encoding='utf-8')
                result = subprocess.run([str(EXE.with_name('sentinel_probe.exe')), '--save-session-prepare', str(path)], capture_output=True, timeout=15)
                self.assertNotEqual(result.returncode, 0, result.stdout.decode(errors='replace'))


if __name__ == '__main__': unittest.main()
