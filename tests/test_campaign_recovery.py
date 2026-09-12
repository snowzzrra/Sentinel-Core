"""C integration: production backup/readback/storage/recovery/Continue adapters.

The RemoteStorage boundary persists exact synthetic native bytes. Every stage is
a separate process. No game, Steam, installed DLL or live save is touched.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

EXE = Path(__file__).resolve().parents[1] / 'build/bin/sentinel_campaign_tests.exe'


@unittest.skipUnless(os.name == 'nt', 'Windows production adapters')
class CampaignRecovery(unittest.TestCase):
    def stage(self, root, mode, defect='native_read_c', difficulty=3, code=0):
        run = subprocess.run([str(EXE), mode, str(root), str(difficulty), defect],
                             capture_output=True, timeout=30)
        output = (run.stdout + run.stderr).decode(errors='replace')
        self.assertEqual(run.returncode, code, output)
        return output

    def create(self, root):
        self.assertIn('explicit verified backup', self.stage(root, 'create'))
        archive = root / (root / 'selected-backup.txt').read_text().strip()
        manifest = (archive / 'transport.manifest').read_text()
        self.assertIn('steam_user=76561198000000001', manifest)
        namespace = manifest.split('namespace=')[1].splitlines()[0]
        target = root / 'remote' / ('ap-' + namespace[:40]) / 'GAME-AUTOSAVE0'
        return archive, target, root / ('ap-' + namespace)

    def digest(self, path):
        return {str(f.relative_to(path)): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in path.rglob('*') if f.is_file()}

    def remove_payload(self, target):
        # Dedicated TemporaryDirectory only; keep remaining exact bytes for
        # production quarantine and preserve all immutable source archives.
        (target / 'game.details').unlink()

    def test_exact_backup_missing_payload_recovery_fresh_continue_writable(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
            root = Path(temp)
            archive, target, _ = self.create(root)
            protected = {'GAME-AUTOSAVE0/game.details': b'vanilla exact bytes', 'PROFILE/profile.bin': b'personal settings and skins'}
            for name, data in protected.items():
                path = root / 'remote' / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            saved = self.digest(archive)
            original = self.digest(target)
            self.remove_payload(target)
            before = self.digest(target)
            self.assertIn('recovery_native_payload_verified', self.stage(root, 'recover'))
            self.assertEqual(self.digest(target), original)
            self.assertEqual(self.digest(archive), saved)
            quarantine = [p for p in root.glob('transport-backup-*') if p != archive]
            self.assertEqual(len(quarantine), 1)
            manifest = (quarantine[0] / 'transport.manifest').read_text()
            for digest in before.values():
                self.assertIn(digest, manifest)
            self.assertIn('checkpoint=2', self.stage(root, 'resume'))
            self.assertNotEqual(self.digest(target), original)
            for name, data in protected.items(): self.assertEqual((root / 'remote' / name).read_bytes(), data)

    def test_exact_target_is_idempotent_no_mutation(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
            root = Path(temp); self.create(root); before = self.digest(root)
            self.assertIn('recovery_already_exact', self.stage(root, 'recover'))
            self.assertEqual(self.digest(root), before)

    def test_known_older_valid_target_is_quarantined_and_advanced_to_exact_backup(self):
        with tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
            root = Path(temp); _, target, _ = self.create(root)
            old = {f.name: f.read_bytes() for f in target.iterdir()}
            record = next(root.rglob('campaign.checkpoint')); old_record = record.read_bytes()
            self.stage(root, 'resume', 'native_read_c_backup_next')
            newer = self.digest(target)
            for name, data in old.items(): (target / name).write_bytes(data)
            record.write_bytes(old_record)
            self.assertIn('recovery_native_payload_verified', self.stage(root, 'recover'))
            self.assertEqual(self.digest(target), newer)

    def test_wrong_owner_options_provider_namespace_archive_refuse_before_mutation(self):
        cases = ['user', 'options', 'provider', 'seed', 'team', 'slot', 'generation',
                 'namespace', 'hash', 'size', 'missing', 'extra', 'alias', 'incomplete', 'basename', 'vanilla', 'legacy']
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
                root = Path(temp); archive, target, _ = self.create(root)
                manifest_path = archive / 'transport.manifest'; manifest = manifest_path.read_text()
                defect = 'native_read_c'; difficulty = 3
                if case == 'user': defect += '_wrong_user'
                elif case == 'options': difficulty = 2
                elif case == 'provider': manifest = manifest.replace('provider_hex=', 'provider_hex=00')
                elif case in ('seed', 'team', 'slot', 'generation', 'namespace'):
                    key = {'seed': 'seed_hex', 'team': 'team', 'slot': 'slot',
                           'generation': 'generation_fingerprint', 'namespace': 'namespace'}[case]
                    old = next(s for s in manifest.splitlines() if s.startswith(key + '='))
                    manifest = manifest.replace(old, old[:-1] + ('0' if old[-1] != '0' else '1'))
                elif case == 'hash':
                    file = archive / 'payload-0.bin'; data = file.read_bytes(); file.write_bytes(bytes([data[0] ^ 1]) + data[1:])
                elif case == 'size':
                    file = archive / 'payload-0.bin'; file.write_bytes(file.read_bytes() + b'x')
                elif case == 'missing': (archive / 'payload-0.bin').unlink()
                elif case == 'extra': (archive / 'extra.bin').write_bytes(b'x')
                elif case == 'alias': (archive / 'payload-0.bin').rename(archive / 'PAYLOAD-0.bin')
                elif case == 'incomplete': manifest_path.unlink()
                elif case == 'basename': (root / 'selected-backup.txt').write_text('../vanilla')
                elif case == 'vanilla':
                    directory = next(s for s in manifest.splitlines() if s.startswith('native_directory='))
                    manifest = manifest.replace(directory, 'native_directory=GAME-AUTOSAVE0')
                elif case == 'legacy':
                    manifest = ''.join(line + '\n' for line in manifest.splitlines()
                                       if not line.startswith(('steam_user=', 'role=', 'provider_hex=', 'contract_hex=', 'checkpoint_hex=')))
                if case != 'incomplete': manifest_path.write_text(manifest, newline='\n')
                self.remove_payload(target); before = self.digest(root / 'remote')
                self.stage(root, 'recover', defect, difficulty, code=2)
                self.assertEqual(self.digest(root / 'remote'), before)
                self.assertFalse(list(root.rglob('recovery.pending')))

    def test_newer_legitimate_and_unknown_content_conflicts_preserve_progress(self):
        for case in ('newer', 'corrupt', 'extra', 'same_number_conflict'):
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
                root = Path(temp); _, target, _ = self.create(root)
                if case == 'newer': self.stage(root, 'resume')
                elif case == 'corrupt': (target / 'game.details').write_bytes(b'unknown authentic-or-corrupt bytes')
                elif case == 'extra': (target / 'unknown.dat').write_bytes(b'do not delete')
                else:
                    checkpoint = next(root.rglob('campaign.checkpoint'))
                    text = checkpoint.read_text()
                    map_line = next(s for s in text.splitlines() if s.startswith('map='))
                    checkpoint.write_text(text.replace(map_line, 'map=another_map'), newline='\n')
                before = self.digest(root / 'remote')
                self.stage(root, 'recover', code=2)
                self.assertEqual(self.digest(root / 'remote'), before)

    def test_crash_each_native_mutation_keeps_admission_closed_and_quarantine_exact(self):
        # Four payload publications + native selection. 0 stops before first
        # mutation after the durable journal. 6/7 fail continuity/completion.
        for boundary in range(8):
            with self.subTest(boundary=boundary), tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
                root = Path(temp); archive, target, _ = self.create(root)
                source = self.digest(archive); self.remove_payload(target)
                before = self.digest(target)
                self.stage(root, 'recover', f'native_read_c_interrupt{boundary}', code=77 if boundary < 6 else 2)
                self.assertTrue(list(root.rglob('recovery.pending')))
                self.assertIn('REFUSED', self.stage(root, 'resume', code=2))
                self.assertEqual(self.digest(archive), source)
                quarantine = next(p for p in root.glob('transport-backup-*') if p != archive)
                manifest = (quarantine / 'transport.manifest').read_text()
                for digest in before.values(): self.assertIn(digest, manifest)


    def test_provider_loss_and_concurrent_target_refuse_at_the_native_startup_boundary(self):
        for defect in ('unavailable', 'provider_loss', 'concurrent'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
                root = Path(temp); archive, target, _ = self.create(root)
                source = self.digest(archive); self.remove_payload(target)
                self.stage(root, 'recover', 'native_read_c_' + defect, code=2)
                self.assertEqual(self.digest(archive), source)
                self.assertEqual(bool(list(root.rglob('recovery.pending'))), defect == 'provider_loss')
                if defect == 'provider_loss': self.stage(root, 'resume', code=2)


if __name__ == '__main__':
    unittest.main()
