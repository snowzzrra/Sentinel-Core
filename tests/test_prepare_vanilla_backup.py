"""Only task-owned temporary trees; no game, account discovery or real saves."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "protection", Path(__file__).parents[1] / "tools" / "prepare_vanilla_backup.py")
protection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protection)


class MountPointTests(unittest.TestCase):
    def test_only_wine_directory_mount_without_reparse_data_is_accepted(self):
        for wine, attrs, tag, query_ok, error, accepted in (
            (True, 0x410, 0xa0000003, False, 4390, True),
            (True, 0x410, 0xa0000003, True, 0, False),
            (False, 0x410, 0xa0000003, False, 4390, False),
            (True, 0x410, 0xa000000c, False, 4390, False),
            (True, 0x400, 0xa0000003, False, 4390, False),
            (True, 0x410, 0xa0000003, False, 5, False),
            (True, 0x410, 0xa0000003, False, 1, False),
        ):
            with self.subTest(wine=wine, attrs=attrs, tag=tag, query_ok=query_ok, error=error):
                api = mock.Mock()
                api.GetProcAddress.return_value = wine
                api.DeviceIoControl.return_value = query_ok
                info = protection.ctypes.create_string_buffer(struct.pack("<II", attrs, tag), 8)
                with mock.patch.object(protection.ctypes, "get_last_error", return_value=error, create=True):
                    self.assertEqual(protection.wine_mount_point(api, 123, info), accepted)

    def test_mount_exception_is_limited_to_ancestor_directories(self):
        api = mock.Mock()
        api.CreateFileW.return_value = 123
        api.GetProcAddress.return_value = True
        api.DeviceIoControl.return_value = False

        def attributes(handle, kind, target, size):
            protection.ctypes.memmove(target, struct.pack("<II", 0x410, 0xa0000003), 8)
            return True

        api.GetFileInformationByHandleEx.side_effect = attributes
        for ancestor, directory, accepted in ((True, True, True), (False, True, False), (True, False, False)):
            with self.subTest(ancestor=ancestor, directory=directory), \
                    mock.patch.object(protection, "kernel", return_value=api), \
                    mock.patch.object(protection.ctypes, "get_last_error", return_value=4390, create=True), \
                    mock.patch.dict(sys.modules, {"msvcrt": mock.Mock()}):
                if accepted:
                    with protection.pinned(Path("fixture"), directory=directory, inspect_streams=False, ancestor=ancestor):
                        pass
                else:
                    with self.assertRaises(protection.Refused):
                        with protection.pinned(Path("fixture"), directory=directory, inspect_streams=False, ancestor=ancestor):
                            self.fail("reparse payload accepted")


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

    def test_unsupported_contract_refuses_before_diagnostic_or_backup_io(self):
        paths = ["prepare", "--steam-account", "12345", "--steam-app-root", str(self.steam),
                 "--local-provider-root", str(self.local), "--backup-directory", str(self.backup),
                 "--ap-root", str(self.root / "ap"), "--uninstall-root", str(self.root / "game-install"),
                 "--diagnostic-file", str(self.root / "diagnostic.json")]
        for revision in ([], ["--slot-data-revision", "0.6.0", "--mod-contract-revision", "2"],
                         ["--slot-data-revision", "0.7.0", "--mod-contract-revision", "1"]):
            with self.subTest(revision=revision), mock.patch.object(protection, "protect") as protect, \
                    mock.patch.object(protection, "explicit_path") as path, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as refusal:
                    protection.main(paths + revision)
                self.assertEqual(refusal.exception.code, 2)
                protect.assert_not_called()
                path.assert_not_called()
        self.assertFalse(self.backup.exists())
        self.assertFalse((self.root / "diagnostic.json").exists())

    def test_imported_helper_refuses_unsupported_contract_before_protection(self):
        for revisions in ({}, {"slot_data_revision": "0.6.0", "mod_contract_revision": 2},
                          {"slot_data_revision": "0.7.0", "mod_contract_revision": 1},
                          {"slot_data_revision": "0.7.0", "mod_contract_revision": "2"}):
            arguments = vars(self.args).copy()
            arguments.pop("slot_data_revision")
            arguments.pop("mod_contract_revision")
            with self.subTest(revisions=revisions), mock.patch.object(protection, "_protect") as protect:
                with self.assertRaisesRegex(protection.Refused, "slot-data 0.7.0 and mod contract 2 required"):
                    protection.protect(argparse.Namespace(**arguments, **revisions))
                protect.assert_not_called()
        self.assertFalse(self.backup.exists())

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
        copied_zone = Path(str(copied) + ":Zone.Identifier")
        self.assertEqual(protection.io_path(copied_zone).read_bytes(), protection.io_path(zone).read_bytes())
        with protection.pinned(archived) as source:
            with self.assertRaises(FileExistsError):
                protection.copy_file(source, copied)
        protection.io_path(copied_zone).write_bytes(b"tampered")
        with self.assertRaisesRegex(protection.Refused, "integrity verification failed"):
            protection.protect(self.args)

    def test_extended_input_and_aliases_refused_before_io_conversion(self):
        for value in ("\\\\?\\" + str(self.backup), str(self.root) + "\\..\\backup",
                      str(self.root) + "\\backup. ", str(self.root) + "\\backup:stream"):
            with self.subTest(value=value), self.assertRaisesRegex(protection.Refused, "without aliases"):
                protection.explicit_path(value)

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

    def test_unknown_stat_link_count_requires_real_single_link_handle(self):
        class UnknownLinks:
            st_nlink = 0
            def __init__(self, value): self.value = value
            def __getattr__(self, name): return getattr(self.value, name)
        info = self.campaign.lstat()
        expected = self.campaign.read_bytes()
        with mock.patch.object(Path, 'lstat', return_value=UnknownLinks(info)):
            self.assertEqual(protection.check_node(self.campaign).st_nlink, 0)
        with mock.patch.object(protection.os, 'fstat', return_value=UnknownLinks(info)):
            with protection.pinned(self.campaign) as source:
                self.assertEqual(source.read(), expected)
        linked = self.campaign.with_name('second-name')
        os.link(self.campaign, linked)
        with self.assertRaises(protection.Refused) as caught:
            protection.check_node(self.campaign)
        self.assertEqual(caught.exception.private_metadata['handle_nlink'], 2)
        self.assertEqual(caught.exception.distinction, 'confirmed_multiple_file_links')
        with self.assertRaises(protection.Refused) as caught:
            protection.verify_links(mock.Mock(GetFileInformationByHandleEx=lambda *unused: False), 0, self.campaign, 'fixture_query_failure', 0)
        self.assertEqual(caught.exception.distinction, 'handle_link_query_failed')

    def test_source_share_conflict_refused_before_destination_created(self):
        with self.campaign.open("rb"):
            with self.assertRaisesRegex(protection.Refused, "pin a path exclusively"):
                protection.protect(self.args)
        self.assertFalse(self.backup.exists())

    def test_stream_introduction_during_copy_is_fail_closed(self):
        zone = Path(str(self.campaign) + ":Zone.Identifier")
        zone.write_bytes(b"[ZoneTransfer]\r\nZoneId=3\r\n")
        unknown = Path(str(self.campaign) + ":during-protection")
        denied = False
        real_copy = protection.copy_file
        def introduce(source, destination):
            nonlocal denied
            result = real_copy(source, destination)
            if destination.name == "game.details":
                try:
                    unknown.write_bytes(b"fixture mutation")
                except PermissionError:
                    denied = True
            return result
        with mock.patch.object(protection, "copy_file", introduce):
            try:
                protection.protect(self.args)
            except protection.Refused:
                self.assertFalse(denied)
                self.assertFalse((self.backup / protection.MANIFEST).exists())
            else:
                self.assertTrue(denied)
                self.assertFalse(unknown.exists())

    def test_exact_metadata_change_before_exclusive_acquisition_is_retained(self):
        real_pin = protection.pinned
        old = self.campaign.stat().st_mtime_ns
        @contextlib.contextmanager
        def changing(path, directory=False, inspect_streams=True, *, ancestor=False):
            if path == self.campaign:
                os.utime(path, ns=(old, old + 1000000000))
            with real_pin(path, directory, inspect_streams, ancestor=ancestor) as stream:
                yield stream
        with mock.patch.object(protection, "pinned", changing):
            with self.assertRaises(protection.Refused) as caught:
                protection.protect(self.args)
        failure = caught.exception
        self.assertEqual(failure.stage, "inventory_acquisition")
        row = next(row for row in failure.private_metadata["differences"] if row["entry"].endswith("game.details"))
        self.assertEqual(row["path_before"]["mtime_ns"], old)
        self.assertEqual(row["path_after"]["mtime_ns"], old + 1000000000)
        self.assertEqual(row["handle_acquired"], row["handle_after"])
        self.assertEqual(failure.summary["handle_path_mismatches"], 1)
        self.assertFalse(self.backup.exists())

    def test_metadata_writer_while_data_handles_are_exclusive_is_detected(self):
        import ctypes
        from ctypes import wintypes
        real_pin = protection.pinned
        changed = False
        @contextlib.contextmanager
        def changing(path, directory=False, inspect_streams=True, *, ancestor=False):
            nonlocal changed
            with real_pin(path, directory, inspect_streams, ancestor=ancestor) as stream:
                if path == self.local / "settings.cfg" and not changed:
                    api = protection.kernel()
                    handle = api.CreateFileW(str(self.campaign), 0x100, 7, None, 3, 0, None)  # write_attributes, share all
                    self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
                    try:
                        ticks = self.campaign.stat().st_mtime_ns // 100 + 116444736000000000 + 10000000
                        value = ctypes.c_uint64(ticks)
                        api.SetFileTime.argtypes = (wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
                        self.assertTrue(api.SetFileTime(handle, None, None, ctypes.byref(value)))
                        changed = True
                    finally:
                        api.CloseHandle(handle)
                yield stream
        with mock.patch.object(protection, "pinned", changing):
            with self.assertRaises(protection.Refused) as caught:
                protection.protect(self.args)
        self.assertTrue(changed)
        self.assertEqual(caught.exception.summary["handle_changed_entries"], 1)
        self.assertIn("mtime_ns", caught.exception.summary["changed_fields"])
        self.assertFalse(self.backup.exists())

    def test_path_disagreement_is_not_accepted_merely_because_handle_is_stable(self):
        original_inventory = protection.inventory
        calls = 0
        def path_disagreement(sources):
            nonlocal calls
            calls += 1
            values = original_inventory(sources)
            if calls == 2:
                key = ("steam_app", "remote/GAME-AUTOSAVE0/game.details")
                old = values[key]
                values[key] = old[:3] + (old[3] + 1,) + old[4:]
            return values
        with mock.patch.object(protection, "inventory", side_effect=path_disagreement):
            with self.assertRaises(protection.Refused) as caught:
                protection.protect(self.args)
        self.assertEqual(caught.exception.summary["changed_fields"], {"size": 1})
        self.assertEqual(caught.exception.summary["handle_changed_entries"], 0)
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

    def test_game_running_refuses_without_copy(self):
        self.stopped.side_effect = protection.Refused("exit DOOM completely before preparation")
        with self.assertRaisesRegex(protection.Refused, "exit DOOM"):
            protection.protect(self.args)
        self.assertFalse(self.backup.exists())

    def test_known_file_stream_roundtrip_and_unrelated_ancestor_stream(self):
        stream = Path(str(self.campaign) + ":Zone.Identifier")
        stream.write_bytes(b"[ZoneTransfer]\nZoneId=3\n")
        ancestor_stream = Path(str(self.root) + ":fixture-ancestor")
        ancestor_stream.write_bytes(b"ancestor content outside protected scope")
        try:
            before = stream.read_bytes()
            receipt = protection.protect(self.args)
            copied = Path(str(self.backup / "steam_app/remote/GAME-AUTOSAVE0/game.details") + ":Zone.Identifier")
            self.assertEqual(copied.read_bytes(), before)
            self.assertEqual(stream.read_bytes(), before)
            self.assertEqual(receipt["files"], 5)
            self.args.action = "verify"
            protection.protect(self.args)
            copied.write_bytes(b"tampered")
            with self.assertRaisesRegex(protection.Refused, "integrity verification failed"):
                protection.protect(self.args)
        finally:
            ancestor_stream.unlink(missing_ok=True)

    def test_os_error_cause_has_paths_errno_operation_and_traceback(self):
        error = FileNotFoundError(2, "fixture source vanished", str(self.campaign))
        with mock.patch.object(protection, "_protect", side_effect=error):
            with self.assertRaises(protection.Refused) as refused:
                protection.protect(self.args)
        self.assertIs(refused.exception.__cause__, error)
        self.assertEqual(refused.exception.filename, str(self.campaign))
        self.assertEqual(refused.exception.errno, 2)
        self.assertIn("FileNotFoundError", refused.exception.private_metadata["traceback"])

    def test_stopped_game_backup_with_steam_allowed(self):
        self.stopped_patch.stop()
        result = protection.protect(self.args)
        self.assertEqual(result["result"], "protective_backup_ready")
        self.assertEqual(self.campaign.read_bytes(), b"original campaign fixture")



if __name__ == "__main__":
    unittest.main()
