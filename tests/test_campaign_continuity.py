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

    def test_pending_native_save_never_reopens_or_becomes_new(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 2, 'pending')
            self.stage('resume', root, 2, 'refuse_configuration')
            self.stage('create', root, 2, 'refuse_configuration')

    def test_dirty_process_refuses_before_reservation(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-campaign-') as root:
            self.stage('create', root, 1, 'dirty')
            self.assertFalse(list(Path(root).rglob('campaign.contract')))

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
