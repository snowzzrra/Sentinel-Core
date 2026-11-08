"""C integration: production backup/readback/storage/recovery/Continue adapters.

The RemoteStorage boundary persists exact synthetic native bytes. Every stage is
a separate process. No game, Steam, installed DLL or live save is touched.
"""
import hashlib
import shutil
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

EXE = Path(os.environ.get('SC_BUILD_DIR', Path(__file__).resolve().parents[1] / 'build')) / 'bin/sentinel_campaign_tests.exe'


@unittest.skipUnless(os.name == 'nt', 'Windows production adapters')
class CampaignRecovery(unittest.TestCase):
    def stage(self, root, mode, defect='native_read_c', difficulty=3, code=0):
        run = subprocess.run([str(EXE), mode, str(root), str(difficulty), defect],
                             capture_output=True, timeout=30)
        output = (run.stdout + run.stderr).decode(errors='replace')
        self.assertEqual(run.returncode, code, output)
        return output

    def create(self, root, defect='native_read_c'):
        self.assertIn('explicit verified backup', self.stage(root, 'create', defect))
        archive = root / (root / 'selected-backup.txt').read_text().strip()
        manifest = (archive / 'transport.manifest').read_text()
        self.assertIn('steam_user=76561198000000001', manifest)
        namespace = manifest.split('namespace=')[1].splitlines()[0]
        target = root / 'remote' / manifest.split('native_directory=')[1].splitlines()[0]
        return archive, target, root / namespace

    def digest(self, path):
        return {str(f.relative_to(path)): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in path.rglob('*') if f.is_file()}

    def remove_payload(self, target):
        # dedicated temporarydirectory only; keep remaining exact bytes for production quarantine and keep all fixed source archives
        (target / 'game.details').unlink()


    def test_retail_pair_does_not_authorize_unknown_or_newer_targets(self):
        for defect in ('unpaired','unknown','primary_corrupt','aux_empty','newer'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix='sentinel-c-retail-') as temp:
                root=Path(temp)
                archive,target,_=self.create(root,'native_read_c_retail_pair')
                if defect=='unpaired':
                    (target/'game.details-BACKUP').unlink()
                elif defect=='unknown':
                    (target/'extra.dat').write_bytes(b'preserve')
                elif defect=='primary_corrupt':
                    (target/'game_duration.dat').write_bytes(b'corrupt')
                elif defect=='aux_empty':
                    (target/'game.details-BACKUP').write_bytes(b'')
                elif defect=='newer':
                    self.stage(root,'resume')
                self.remove_payload(target)
                before=self.digest(target)
                self.stage(root,'recover',code=2)
                self.assertEqual(self.digest(target),before)
                self.assertFalse(list(root.rglob('recovery.pending')))

    def test_retail_auxiliary_concurrency_and_interruption_preserve_guard(self):
        for defect in ('native_read_c_concurrent_auxiliary',*[f'native_read_c_interrupt{i}' for i in range(4)]):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix='sentinel-c-retail-') as temp:
                root=Path(temp)
                archive,target,_=self.create(root,'native_read_c_retail_pair')
                self.remove_payload(target)
                before=self.digest(target)
                saved=self.digest(archive)
                concurrency=defect.endswith('auxiliary')
                output=self.stage(root,'recover',defect,code=2 if concurrency else 77)
                self.assertEqual(self.digest(archive),saved)
                self.assertEqual(bool(list(root.rglob('recovery.pending'))),not concurrency)
                if concurrency:
                    self.assertIn('recovery_concurrent_target_change',output)
                else:
                    self.stage(root,'resume',code=2)
                    for name,h in before.items():
                        if name.endswith('-BACKUP'):
                            self.assertEqual(self.digest(target)[name],h)


    def test_wrong_owner_options_provider_namespace_archive_refuse_before_mutation(self):
        cases = ['user', 'options', 'provider', 'seed', 'team', 'slot', 'generation',
                 'namespace', 'hash', 'size', 'missing', 'extra', 'alias', 'incomplete', 'basename', 'vanilla', 'legacy']
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
                root = Path(temp)
                archive, target, _ = self.create(root)
                manifest_path = archive / 'transport.manifest'
                manifest = manifest_path.read_text()
                defect = 'native_read_c'
                difficulty = 3
                if case == 'user':
                    defect += '_wrong_user'
                elif case == 'options':
                    difficulty = 2
                elif case == 'provider':
                    manifest = manifest.replace('provider_hex=', 'provider_hex=00')
                elif case in ('seed', 'team', 'slot', 'generation', 'namespace'):
                    key = {'seed': 'seed_hex', 'team': 'team', 'slot': 'slot',
                           'generation': 'generation_fingerprint', 'namespace': 'namespace'}[case]
                    old = next(s for s in manifest.splitlines() if s.startswith(key + '='))
                    manifest = manifest.replace(old, old[:-1] + ('0' if old[-1] != '0' else '1'))
                elif case == 'hash':
                    file = archive / 'payload-0.bin'
                    data = file.read_bytes()
                    file.write_bytes(bytes([data[0] ^ 1]) + data[1:])
                elif case == 'size':
                    file = archive / 'payload-0.bin'
                    file.write_bytes(file.read_bytes() + b'x')
                elif case == 'missing':
                    (archive / 'payload-0.bin').unlink()
                elif case == 'extra':
                    (archive / 'extra.bin').write_bytes(b'x')
                elif case == 'alias':
                    (archive / 'payload-0.bin').rename(archive / 'PAYLOAD-0.bin')
                elif case == 'incomplete':
                    manifest_path.unlink()
                elif case == 'basename':
                    (root / 'selected-backup.txt').write_text('../vanilla')
                elif case == 'vanilla':
                    directory = next(s for s in manifest.splitlines() if s.startswith('native_directory='))
                    manifest = manifest.replace(directory, 'native_directory=GAME-AUTOSAVE0')
                elif case == 'legacy':
                    manifest = ''.join(line + '\n' for line in manifest.splitlines()
                                       if not line.startswith(('steam_user=', 'role=', 'provider_hex=', 'contract_hex=', 'checkpoint_hex=')))
                if case != 'incomplete':
                    manifest_path.write_text(manifest, newline='\n')
                self.remove_payload(target)
                before = self.digest(root / 'remote')
                self.stage(root, 'recover', defect, difficulty, code=2)
                self.assertEqual(self.digest(root / 'remote'), before)
                self.assertFalse(list(root.rglob('recovery.pending')))

    def test_newer_legitimate_and_unknown_content_conflicts_preserve_progress(self):
        for case in ('newer', 'corrupt', 'extra', 'same_number_conflict'):
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='sentinel-c-') as temp:
                root = Path(temp)
                _, target, _ = self.create(root)
                if case == 'newer':
                    self.stage(root, 'resume')
                elif case == 'corrupt':
                    (target / 'game.details').write_bytes(b'unknown authentic-or-corrupt bytes')
                elif case == 'extra':
                    (target / 'unknown.dat').write_bytes(b'do not delete')
                else:
                    pending = next(root.rglob('checkpoint-A.pending'))
                    text = pending.read_bytes()
                    archive_name = text.split(b'archive=')[1].splitlines()[0]
                    archive = root / archive_name.decode()
                    other = root / (archive.name[:-32] + 'e' * 32)
                    shutil.copytree(archive, other)
                    record = bytes.fromhex(text.split(b'checkpoint_hex=')[1].splitlines()[0].decode())
                    altered = record.replace(b'map=game/hub/hub', b'map=another_map')
                    manifest = (other / 'transport.manifest').read_bytes().replace(record.hex().encode(), altered.hex().encode())
                    (other / 'transport.manifest').write_bytes(manifest)
                    body = text[:-72].replace(archive_name, other.name.encode()).replace(record.hex().encode(), altered.hex().encode())
                    old_hash = body.split(b'manifest_sha256=')[1].splitlines()[0]
                    body = body.replace(old_hash, hashlib.sha256(manifest).hexdigest().encode())
                    def signed(body):
                        return body + b'sha256=' + hashlib.sha256(body).hexdigest().encode() + b'\n'
                    pending.write_bytes(signed(body))
                    receipt = signed(b'sentinel-checkpoint-receipt-v1\npending_sha256=' + hashlib.sha256(body).hexdigest().encode() +
                                     b'\nnative_success=1\ncallbacks=1\nreadback_terminal=1\ntransition=1\n')
                    pending.with_suffix('.receipt').write_bytes(receipt)
                    pending.with_suffix('.commit').write_bytes(signed(b'sentinel-checkpoint-commit-v1\npending_sha256=' +
                        hashlib.sha256(body).hexdigest().encode() + b'\nreceipt_sha256=' + receipt[-65:-1] + b'\n'))
                before = self.digest(root / 'remote')
                self.stage(root, 'recover', code=2)
                self.assertEqual(self.digest(root / 'remote'), before)


if __name__ == '__main__':
    unittest.main()
