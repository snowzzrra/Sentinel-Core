import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
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
                                               dir=Path(__file__).resolve().parents[1] / ".tmp")
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
        self.args = argparse.Namespace(action="prepare", steam_account="12345", slot_data_revision="0.7.0", mod_contract_revision=2,
            steam_app_root=str(self.steam), local_provider_root=str(self.local),
            backup_directory=str(self.backup), ap_root=str(self.root / "ap"),
            uninstall_root=[str(self.root / "game-install"), str(self.root / "mod-install")])
        # The fixture's writers are this test only. Never bypass the production
        # process check via any user-visible argument or environment setting.
        stopped = mock.patch.object(protection, "require_stopped")
        self.stopped_patch = stopped
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

    def test_long_paths_roundtrip_preserves_identity_streams_and_create_only(self):
        archived = self.local / "quarantine" / "failed" / ("archived_" + "x" * 170 + ".txt")
        protection.io_path(archived.parent).mkdir(parents=True)
        protection.io_path(archived).write_bytes(b"retained diagnostic fixture")
        ancestor_stream = Path(str(self.root) + ":fixture-ancestor")
        ancestor_stream.write_bytes(b"outside protected scope")
        self.addCleanup(ancestor_stream.unlink)
        zone = Path(str(archived) + ":Zone.Identifier")
        protection.io_path(zone).write_bytes(b"[ZoneTransfer]\nZoneId=3\n")
        self.backup = self.backup.parent / ("namespace_" + "a" * 110) / ("run_" + "b" * 70)
        protection.io_path(self.backup.parent).mkdir()
        self.args.backup_directory = str(self.backup)
        copied = self.backup / "local_provider" / archived.relative_to(self.local)
        self.assertGreater(len(str(archived)), 260)
        self.assertGreater(len(str(self.backup / protection.MANIFEST)), 260)
        self.assertGreater(len(str(copied)), 260)
        real_open = Path.open
        real_listdir = os.listdir
        def extended_open(path, *args, **kwargs):
            self.assertTrue(str(path).startswith("\\\\?\\"), str(path))
            return real_open(path, *args, **kwargs)
        def extended_listdir(path):
            self.assertTrue(str(path).startswith("\\\\?\\"), str(path))
            return real_listdir(path)
        with mock.patch.object(Path, "open", extended_open), mock.patch.object(os, "listdir", extended_listdir):
            receipt = protection.protect(self.args)
            self.assertEqual(receipt["reference_directory"], str(self.backup))
            self.assertTrue(protection.protect(self.args)["reused"])
            self.args.action = "verify"
            protection.protect(self.args)
        manifest = json.loads(protection.io_path(self.backup / protection.MANIFEST).read_text())
        self.assertEqual(manifest["origins"]["local_provider"], str(self.local))
        self.assertFalse(any("\\\\?\\" in entry["relative"] for entry in manifest["files"]))
        self.assertEqual(protection.io_path(archived).read_bytes(), b"retained diagnostic fixture")
        self.assertEqual(protection.io_path(copied).read_bytes(), b"retained diagnostic fixture")
        self.assertEqual(ancestor_stream.read_bytes(), b"outside protected scope")
        copied_zone = Path(str(copied) + ":Zone.Identifier")
        self.assertEqual(protection.io_path(copied_zone).read_bytes(), protection.io_path(zone).read_bytes())
        with protection.pinned(archived) as source:
            with self.assertRaises(FileExistsError):
                protection.copy_file(source, copied)
        protection.io_path(copied_zone).write_bytes(b"tampered")
        with self.assertRaisesRegex(protection.Refused, "integrity verification failed"):
            protection.protect(self.args)


    def test_later_original_progress_gets_new_reference_without_overwriting_first(self):
        protection.protect(self.args)
        first = (self.backup / "steam_app" / "remote" / "GAME-AUTOSAVE0" / "game.details").read_bytes()
        self.campaign.write_bytes(b"later legitimate vanilla progress")
        refreshed = protection.protect(self.args)
        self.assertNotEqual(refreshed["reference_directory"], str(self.backup))
        self.assertEqual(refreshed["historical_comparison"]["campaign_counts"]["content_changed"], 1)
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
            with self.assertRaisesRegex(protection.Refused, "OSError"):
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
        with self.assertRaisesRegex(protection.Refused, "unsupported protected named stream") as refused:
            protection.protect(self.args)
        self.assertEqual(refused.exception.private_metadata["path"], str(self.campaign))
        self.assertEqual(refused.exception.private_metadata["stream"], ":fixture-extra:$DATA")
        ads.unlink()
        os.link(self.campaign, self.local / "hardlink")
        with self.assertRaisesRegex(protection.Refused, "hard-linked"):
            protection.protect(self.args)
        self.assertFalse(self.backup.exists())

    def test_junction_ancestor_refused_without_source_changes(self):
        link = self.root / "provider-link"
        link.mkdir()
        target = str(self.local.resolve())
        substitute = ("\\??\\" + target).encode("utf-16le")
        printable = target.encode("utf-16le")
        names = substitute + b"\0\0" + printable + b"\0\0"
        data = struct.pack("<IHHHHHH", 0xa0000003, 8 + len(names), 0, 0,
                           len(substitute), len(substitute) + 2, len(printable)) + names
        api = protection.kernel()
        handle = api.CreateFileW(str(link), 0x40000000, 0, None, 3, 0x02200000, None)
        self.assertNotEqual(handle, protection.ctypes.c_void_p(-1).value)
        try:
            returned = protection.wintypes.DWORD()
            self.assertTrue(api.DeviceIoControl(handle, 0x900a4, data, len(data), None, 0,
                                                protection.ctypes.byref(returned), None))
        finally:
            api.CloseHandle(handle)
        original = self.snapshot()
        self.args.local_provider_root = str(link)
        try:
            with self.assertRaises(protection.Refused):
                protection.protect(self.args)
            self.assertFalse(self.backup.exists())
            self.assertEqual(self.snapshot(), original)
        finally:
            link.rmdir()


if __name__ == "__main__":
    unittest.main()
