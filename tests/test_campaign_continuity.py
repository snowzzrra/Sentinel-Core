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
    def test_cross_map_save_and_wup_share_the_admitted_session(self):
        # Stable native ownership regression: the second map must not re-enter
        # NewGame/Continue admission. The fixture exercises the real dispatch,
        # lifecycle, PROFILE, writer, SDK and readback adapters in one Session.
        with tempfile.TemporaryDirectory(prefix='sentinel-cross-map-') as root:
            created = self.stage('create', root, 3, 'cross_map')
            self.assertIn('map=game/sp/e1m1_intro/e1m1_intro generation=3 checkpoint=3 WUP=6', created)
            self.assertIn('map=game/sp/e1m2_battle/e1m2_battle generation=4 checkpoint=5 WUP=9', created)
            resumed = self.stage('resume', root, 3, 'cross_map')
            self.assertIn('map=game/sp/e1m3_cult/e1m3_cult generation=3 checkpoint=8 WUP=6', resumed)
        with tempfile.TemporaryDirectory(prefix='sentinel-cross-map-gap-') as root:
            self.assertIn('unobserved generation gap refused', self.stage('create', root, 3, 'cross_map_gap'))

    def test_startup_refusal_keeps_downstream_parser_closed(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-startup-parser-') as root:
            self.stage('create', root, 3, 'startup_parser')

    def test_profile_completion_between_native_campaign_payloads(self):
        # Stable native boundary: PROFILE can complete after duration and before
        # details. Exercise the real provider futures, then travel and reopen.
        with tempfile.TemporaryDirectory(prefix='sentinel-profile-overlap-') as root:
            created = self.stage('create', root, 3, 'cross_map_profile_overlap')
            self.assertIn('map=game/sp/e1m2_battle/e1m2_battle generation=4 checkpoint=5 WUP=9', created)
            resumed = self.stage('resume', root, 3, 'cross_map_profile_overlap')
            self.assertIn('checkpoint=8 WUP=6 session=admitted', resumed)
        with tempfile.TemporaryDirectory(prefix='sentinel-profile-overlap-failure-') as root:
            self.assertIn('deferred PROFILE cannot certify failed gameplay save',
                          self.stage('create', root, 3, 'cross_map_profile_overlap_failed_save'))

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

    def test_saved_session_rehydrates_menu_then_continues_again(self):
        for defect in ('native_read_menu_cycle', 'native_read_menu_cycle_hash'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix='sentinel-menu-cycle-') as root:
                self.stage('create', root, 3, 'native_read')
                result = self.stage('resume', root, 3, defect)
                self.assertIn('repeated Continue rejects changed payload' if defect.endswith('_hash')
                              else 'shell/catalog/Continue/save/catalog checkpoint=3', result)

    def test_native_mission_select_keeps_source_verification_and_exact_destination(self):
        for defect in ('native_read_mission', 'native_read_mission_wrong_map'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix='sentinel-menu-mission-') as root:
                self.stage('create', root, 3, 'native_read')
                result = self.stage('resume', root, 3, defect)
                self.assertIn('rejects a different destination' if defect.endswith('wrong_map')
                              else 'menu/read/parser/Continue/save checkpoint=2', result)

    def test_native_hub_action_presentation(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-hub-actions-') as root:
            result = self.stage('create', root, 2)
            self.assertIn('Hub action labels, late availability, entry focus and manual focus retention', result)

    def test_native_continue_restores_persisted_mission_snapshot(self):
        for suffix in ('', '_missing', '_wrong', '_assign'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory(prefix='sentinel-mission-checkpoint-') as root:
                self.stage('create', root, 2, 'native_read_checkpoint')
                checkpoint, = Path(root).rglob('campaign.checkpoint')
                # Explicit fixture migration; no inference from arbitrary map names.
                original = checkpoint.read_text()
                original = original.replace('map=game/hub/hub\n', 'map=game/sp/e2m2_base/e2m2_base\n')
                original = original.replace('subtype=1\n', 'subtype=2\n')
                checkpoint.write_text(original, newline='\n')
                result = self.stage('resume', root, 2, 'native_read_checkpoint' + suffix)
                if suffix:
                    self.assertIn('persisted mission checkpoint refusal', result)
                    self.assertEqual(checkpoint.read_text(), original)
                else:
                    self.assertIn('dual Hub/ARC snapshot checkpoint preserved', result)
                    self.assertIn('map=game/sp/e2m2_base/e2m2_base\nsubtype=2\n', checkpoint.read_text())
                    self.stage('resume', root, 2, 'native_read_checkpoint_resume')

    def test_native_partial_save_preserves_complete_inventory(self):
        for suffix in ('', '_backup', '_hash'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory(prefix='sentinel-partial-save-') as root:
                self.stage('create', root, 2, 'native_read_c_retail_pair')
                result = self.stage('resume', root, 2, 'native_read_delta' + suffix)
                if suffix == '_hash':
                    self.assertIn('retained-file tampering refused', result)
                else:
                    self.assertIn('complete inventory/readback/catalog/Continue/backup', result)
                    checkpoint, = Path(root).rglob('campaign.checkpoint')
                    self.assertIn('files=2', checkpoint.read_text())
                    resumed = self.stage('resume', root, 2, 'native_read_delta_resume')
                    self.assertIn('separate-process partial-save catalog/parser/resume', resumed)

    def test_native_mission_select_shell_presave(self):
        for suffix in ('', '_unarmed', '_generation', '_foreign', '_failure'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory(prefix='sentinel-menu-presave-') as root:
                self.stage('create', root, 2, 'native_read')
                result = self.stage('resume', root, 2, 'native_read_shell' + suffix)
                self.assertIn('owned Mission Select shell save/readback/load and boundary refusals', result)

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
