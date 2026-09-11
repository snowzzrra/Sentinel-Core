"""Only task-owned temporary trees; no game, account discovery or real saves."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "protection", Path(__file__).parents[1] / "tools" / "prepare_vanilla_backup.py")
protection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protection)


@unittest.skipUnless(os.name == "nt", "Windows exclusive-handle contract")
class ProtectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sentinel-vanilla-fixture-",
                                               dir=Path(__file__).parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.steam = self.root / "steam" / "userdata" / "12345" / "782330"
        self.local = self.root / "local-provider"
        self.backup = self.root / "protected" / "first"
        self.backup.parent.mkdir()
        (self.steam / "remote" / "GAME-AUTOSAVE0").mkdir(parents=True)
        self.local.mkdir()
        self.campaign = self.steam / "remote" / "GAME-AUTOSAVE0" / "game.details"
        self.campaign.write_bytes(b"original campaign fixture")
        (self.steam / "remote" / "PROFILE").write_bytes(b"original selection fixture")
        (self.steam / "remotecache.vdf").write_bytes(b"fixture local cache metadata")
        (self.local / "settings.cfg").write_bytes(b"fixture settings")
        (self.local / "empty-directory").mkdir()
        self.args = argparse.Namespace(action="prepare", steam_account="12345",
            steam_app_root=str(self.steam), local_provider_root=str(self.local),
            backup_directory=str(self.backup), ap_root=str(self.root / "ap"),
            uninstall_root=[str(self.root / "game-install"), str(self.root / "mod-install")])
        # The fixture's writers are this test only. Never bypass the production
        # process check via any user-visible argument or environment setting.
        stopped = mock.patch.object(protection, "require_stopped")
        self.stopped = stopped.start()
        self.addCleanup(stopped.stop)

    def snapshot(self):
        return {str(p): (p.read_bytes(), p.stat().st_mtime_ns)
                for root in (self.steam, self.local) for p in root.rglob("*") if p.is_file()}

    def test_complete_readback_preserves_sources_and_reuses_first(self):
        original = self.snapshot()
        self.assertEqual(protection.protect(self.args)["result"], "protective_backup_ready")
        manifest_path = self.backup / protection.MANIFEST
        first = manifest_path.read_bytes(), manifest_path.stat().st_mtime_ns
        self.assertEqual(len(json.loads(first[0])["files"]), 4)
        self.assertTrue((self.backup / "local_provider" / "empty-directory").is_dir())
        self.assertEqual(protection.protect(self.args)["result"], "protective_backup_ready")
        self.assertEqual(first, (manifest_path.read_bytes(), manifest_path.stat().st_mtime_ns))
        self.assertEqual(self.snapshot(), original)
        self.args.action = "verify"
        self.assertEqual(protection.protect(self.args)["result"], "protective_backup_verified")

    def test_later_original_progress_refused_without_overwriting_first(self):
        protection.protect(self.args)
        first = (self.backup / "steam_app" / "remote" / "GAME-AUTOSAVE0" / "game.details").read_bytes()
        self.campaign.write_bytes(b"later legitimate vanilla progress")
        with self.assertRaisesRegex(protection.Refused, "originals differ"):
            protection.protect(self.args)
        self.assertEqual(self.campaign.read_bytes(), b"later legitimate vanilla progress")
        self.assertEqual((self.backup / "steam_app" / "remote" / "GAME-AUTOSAVE0" / "game.details").read_bytes(), first)
        self.args.action = "verify"
        self.assertEqual(protection.protect(self.args)["result"], "protective_backup_verified")

    def test_interrupted_copy_retained_and_never_resumed(self):
        original = self.snapshot()
        real_copy = protection.copy_file
        def interrupted(source, destination):
            real_copy(source, destination)
            raise OSError("fixture interrupted write")
        with mock.patch.object(protection, "copy_file", side_effect=interrupted):
            with self.assertRaises(OSError):
                protection.protect(self.args)
        self.assertTrue(self.backup.is_dir())
        self.assertFalse((self.backup / protection.MANIFEST).exists())
        with self.assertRaisesRegex(protection.Refused, "incomplete protection"):
            protection.protect(self.args)
        self.assertEqual(self.snapshot(), original)

    def test_source_share_conflict_refused_before_destination_created(self):
        with self.campaign.open("rb"):
            with self.assertRaisesRegex(protection.Refused, "pin a path exclusively"):
                protection.protect(self.args)
        self.assertFalse(self.backup.exists())

    def test_source_file_set_change_during_copy_refused(self):
        real_copy = protection.copy_file
        changed = False
        def changing(source, destination):
            nonlocal changed
            value = real_copy(source, destination)
            if not changed:
                (self.local / "appeared.cfg").write_bytes(b"concurrent fixture writer")
                changed = True
            return value
        with mock.patch.object(protection, "copy_file", side_effect=changing):
            with self.assertRaisesRegex(protection.Refused, "source changed"):
                protection.protect(self.args)
        self.assertFalse((self.backup / protection.MANIFEST).exists())

    def test_backup_tamper_and_extra_file_refused(self):
        protection.protect(self.args)
        payload = self.backup / "steam_app" / "remote" / "PROFILE"
        original = payload.read_bytes()
        payload.write_bytes(b"damaged")
        self.args.action = "verify"
        with self.assertRaisesRegex(protection.Refused, "integrity"):
            protection.protect(self.args)
        payload.write_bytes(original)
        (self.backup / "unexpected.txt").write_bytes(b"fixture")
        with self.assertRaisesRegex(protection.Refused, "set differs"):
            protection.protect(self.args)

    def test_account_and_destination_scope_refusal(self):
        self.args.steam_account = "67890"
        with self.assertRaisesRegex(protection.Refused, "Steam origin"):
            protection.protect(self.args)
        self.args.steam_account = "12345"
        for destination in (self.local / "backup", Path(self.args.ap_root) / "backup",
                            Path(self.args.uninstall_root[0]) / "backup", self.root):
            with self.subTest(destination=destination):
                self.args.backup_directory = str(destination)
                with self.assertRaisesRegex(protection.Refused, "separate"):
                    protection.protect(self.args)

    def test_named_stream_and_hardlink_sources_refused(self):
        ads = Path(str(self.campaign) + ":fixture-extra")
        ads.write_bytes(b"must not silently drop stream")
        with self.assertRaisesRegex(protection.Refused, "named data streams"):
            protection.protect(self.args)
        ads.unlink()
        os.link(self.campaign, self.local / "hardlink")
        with self.assertRaisesRegex(protection.Refused, "hard-linked"):
            protection.protect(self.args)
        self.assertFalse(self.backup.exists())

    def test_game_or_steam_running_refuses_without_copy(self):
        self.stopped.side_effect = protection.Refused("exit DOOM and Steam completely before preparation")
        with self.assertRaisesRegex(protection.Refused, "exit DOOM"):
            protection.protect(self.args)
        self.assertFalse(self.backup.exists())

    def test_cli_failure_is_nonzero_and_does_not_disclose_source_filename(self):
        self.args.backup_directory = str(self.local / "unsafe")
        argv = [self.args.action]
        for key, value in vars(self.args).items():
            if key == "action":
                continue
            for entry in value if isinstance(value, list) else [value]:
                argv.extend(["--" + key.replace("_", "-"), entry])
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            self.assertEqual(protection.main(argv), 1)
        self.assertEqual(json.loads(output.getvalue())["result"], "protection_refused")
        self.assertNotIn(str(self.local), output.getvalue())


if __name__ == "__main__":
    unittest.main()
