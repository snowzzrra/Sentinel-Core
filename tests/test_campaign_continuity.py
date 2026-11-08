"""Production campaign/storage/SDK tracking with synthetic native boundaries.

Each stage is a separate Windows host process. Only TemporaryDirectory fixtures
are touched; the target is sentinel_campaign_tests, never DOOM or Steam.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

EXE = Path(os.environ.get('SC_BUILD_DIR', Path(__file__).resolve().parents[1] / 'build')) / 'bin/sentinel_campaign_tests.exe'


def test_master_level_checkpoint_and_restart_keep_stage_identity(tmp_path):
    for action, defect in [('create', 'restart_master_postsave'), ('resume', 'restart_master_loaded')]:
        result = subprocess.run([str(EXE), action, str(tmp_path), '3', defect], capture_output=True, timeout=15)
        assert result.returncode == 0, (result.stdout + result.stderr).decode(errors='replace')
    checkpoint, = tmp_path.rglob('campaign.checkpoint')
    record = checkpoint_text(checkpoint)
    assert '\nsubtype=6\nstage=7776006\n' in record


def checkpoint_text(path):
    records = []
    for pending in path.parent.glob('checkpoint-*.pending'):
        text = pending.read_text()
        if '\ncheckpoint_hex=' in text and pending.with_suffix('.commit').exists():
            record = bytes.fromhex(text.split('checkpoint_hex=')[1].splitlines()[0]).decode()
            records.append((int(record.split('\ncheckpoint=')[1].splitlines()[0]), record))
    return max(records)[1] if records else path.read_text()


@unittest.skipUnless(os.name == 'nt', 'Windows native contracts')
class CampaignContinuity(unittest.TestCase):
    def test_final_death_survives_exit_and_only_confirmed_load_clears_restart(self):
        for mode in ('restart_pending', 'restart_postsave', 'restart_recovered'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='sentinel-restart-') as root:
                self.stage('create', root, 3, mode)
                pending, = Path(root).rglob('stage.restart.pending')
                if mode == 'restart_recovered':
                    head, _, tail = pending.read_bytes().partition(b'\ncheckpoint=')
                    checkpoint, separator, rest = tail.partition(b'\n')
                    pending.write_bytes(head + b'\ncheckpoint=' + str(int(checkpoint) + 1).encode() + separator + rest)
                marker = pending.read_bytes()
                self.stage('resume', root, 3, 'restart_failed')
                self.assertEqual(pending.read_bytes().split(b'\ncheckpoint=')[0], marker.split(b'\ncheckpoint=')[0])
                marker = pending.read_bytes()
                self.assertFalse(pending.with_name('stage.restart.loaded').exists())
                pending.with_name('stage.restart.loaded').write_bytes(b'')
                self.stage('resume', root, 3, 'restart_loaded')
                self.assertEqual(pending.with_name('stage.restart.loaded').read_bytes(), marker)
                self.stage('resume', root, 3, 'restart_save')
                self.stage('resume', root, 3, 'restart_normal')

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


    def stage(self, mode, root, difficulty, defect=None):
        command = [str(EXE), mode, str(root), str(difficulty)]
        if defect:
            command.append(defect)
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


    def test_pending_native_save_never_reopens_or_becomes_new(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 2, 'pending')
            self.stage('resume', root, 2, 'refuse_configuration')
            self.stage('create', root, 2, 'refuse_configuration')


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
                    self.assertIn('files=2', checkpoint_text(checkpoint))
                    resumed = self.stage('resume', root, 2, 'native_read_delta_resume')
                    self.assertIn('separate-process partial-save catalog/parser/resume', resumed)

    def test_owned_provider_rollback_requires_confirmed_hashes(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-provider-rollback-') as root:
            self.stage('create', root, 2, 'native_read_c_rollback')
            result = self.stage('resume', root, 2, 'native_read_c_rollback')
            self.assertIn('provider rollback without confirmed hashes is refused', result)


    def test_incomplete_and_changed_metadata_refuse(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 3)
            checkpoint, = Path(root).rglob('campaign.checkpoint')
            original = checkpoint.read_bytes()
            checkpoint.write_bytes(original[:-1])
            self.stage('resume', root, 3, 'refuse_configuration')

    def test_descriptor_requires_explicit_supported_immutable_options(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-descriptor-') as root:
            path = Path(root) / 'input.txt'
            valid = ('sentinel-test-session-v3\nseed_hex=74657374\nteam=0\nslot=1\ngeneration_fingerprint=' + 'b' * 64 +
                '\nslot_data_revision=0.7.0\nsession_mod_contract_revision=2\nprovenance=synthetic-fixture\nroot=' + root +
                '\ncampaign=base\nstarting_stage=base_start\ndifficulty=3\nintent=create\n')
            for invalid in (valid.replace('difficulty=3\n', ''), valid.replace('difficulty=3', 'difficulty=4'),
                    valid.replace('difficulty=3', 'difficulty=-1'), valid.replace('intent=create', 'intent=guess'),
                    valid.replace('campaign=base', 'campaign=tag1')):
                path.write_text(invalid, encoding='utf-8')
                result = subprocess.run([str(EXE.with_name('sentinel_probe.exe')), '--save-session-prepare', str(path)], capture_output=True, timeout=15)
                self.assertNotEqual(result.returncode, 0, result.stdout.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()
