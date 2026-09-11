"""Synthetic owned roots only; native file pinning and hashing remain enabled."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


tools = Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(tools))
try:
    spec = importlib.util.spec_from_file_location("comparison", tools / "compare_vanilla_campaign.py")
    comparison = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(comparison)
finally:
    sys.path.pop(0)
protection = comparison.protection


@unittest.skipUnless(os.name == "nt", "Windows exclusive-handle contract")
class CampaignComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="campaign-comparison-fixture-", dir=Path(__file__).parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.steam = self.root / "userdata" / "12345" / "782330"
        self.local = self.root / "local-provider"
        self.local.mkdir()
        self.campaigns = []
        for slot in ("GAME-AUTOSAVE0", "DLC1-AUTOSAVE11", "DLC2-AUTOSAVE10"):
            directory = self.steam / "remote" / slot
            directory.mkdir(parents=True)
            path = directory / "game.details"
            path.write_bytes(b"fixture original campaign")
            self.campaigns.append(path)
        self.ap_campaign = self.steam / "remote" / ("ap-" + "a" * 40) / "GAME-AUTOSAVE0" / "game.details"
        self.ap_campaign.parent.mkdir(parents=True)
        self.ap_campaign.write_bytes(b"fixture AP payload")
        (self.steam / "remote" / "PROFILE").write_bytes(b"fixture selection")
        (self.local / "settings.cfg").write_bytes(b"fixture settings")
        self.backup = self.root / "first-backup"
        self.args = argparse.Namespace(action="prepare", steam_account="12345", steam_app_root=str(self.steam),
            local_provider_root=str(self.local), backup_directory=str(self.backup),
            ap_root=str(self.root / "ap"), uninstall_root=[str(self.root / "fixture-install")])
        stopped = mock.patch.object(protection, "require_stopped")
        self.stopped = stopped.start()
        self.addCleanup(stopped.stop)
        protection.protect(self.args)

    def snapshot(self):
        return {str(path): (path.read_bytes(), path.stat().st_mtime_ns)
                for directory in (self.steam, self.local, self.backup)
                for path in directory.rglob("*") if path.is_file()}

    def test_unchanged_is_read_only_and_selection_remains_manual(self):
        before = self.snapshot()
        result = comparison.compare(self.args)
        self.assertEqual(result["result"], "vanilla_campaign_unchanged")
        self.assertEqual(result["baseline_campaign_files"], 3)
        self.assertEqual(result["selection_verification"], "manual_vanilla_menu_check_required")
        self.assertEqual(before, self.snapshot())

    def test_campaign_edit_add_and_remove_report_only_aggregate_differences(self):
        for kind in ("modified", "added", "removed"):
            with self.subTest(kind=kind):
                path = self.campaigns[0]
                original = path.read_bytes()
                extra = path.parent / "additional.details"
                if kind == "modified":
                    path.write_bytes(b"fixture changed campaign")
                elif kind == "added":
                    extra.write_bytes(b"fixture added campaign data")
                else:
                    path.unlink()
                result = comparison.compare(self.args)
                self.assertEqual(result["result"], "vanilla_campaign_changed")
                self.assertEqual(result[kind], 1)
                self.assertEqual(sum(result[key] for key in ("modified", "added", "removed")), 1)
                self.assertNotIn(str(path), json.dumps(result))
                if extra.exists():
                    extra.unlink()
                path.write_bytes(original)

    def test_ap_same_basename_and_profile_settings_changes_are_not_vanilla_campaign_changes(self):
        self.ap_campaign.write_bytes(b"updated AP payload with identical filename")
        (self.steam / "remote" / "PROFILE").write_bytes(b"selection changed by menu")
        (self.local / "settings.cfg").write_bytes(b"updated settings")
        extra = self.steam / "remote" / "GAME-AUTOSAVE12"
        extra.mkdir()
        (extra / "game.details").write_bytes(b"outside native slots 0 through 11")
        self.assertEqual(comparison.compare(self.args)["result"], "vanilla_campaign_unchanged")

    def test_corrupt_protective_backup_refuses_comparison(self):
        (self.backup / "steam_app" / "remote" / "PROFILE").write_bytes(b"corrupt protective payload")
        with self.assertRaisesRegex(protection.Refused, "integrity"):
            comparison.compare(self.args)

    def test_no_original_vanilla_campaign_is_inconclusive(self):
        for path in self.campaigns:
            path.unlink()
        self.args.backup_directory = str(self.root / "campaign-empty-backup")
        protection.protect(self.args)
        with self.assertRaisesRegex(protection.Refused, "inconclusive"):
            comparison.compare(self.args)

    def test_source_share_conflict_and_running_game_refuse(self):
        with self.campaigns[0].open("rb"):
            with self.assertRaisesRegex(protection.Refused, "pin a path exclusively"):
                comparison.compare(self.args)
        self.stopped.side_effect = protection.Refused("exit DOOM and Steam completely before preparation")
        with self.assertRaisesRegex(protection.Refused, "exit DOOM"):
            comparison.compare(self.args)

    def test_changed_campaign_cli_is_nonzero_without_disclosing_paths(self):
        self.campaigns[0].write_bytes(b"changed")
        argv = []
        for name in ("steam_account", "steam_app_root", "local_provider_root", "backup_directory"):
            argv.extend(["--" + name.replace("_", "-"), getattr(self.args, name)])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(comparison.main(argv), 1)
        self.assertEqual(json.loads(output.getvalue())["modified"], 1)
        self.assertNotIn(str(self.root), output.getvalue())


if __name__ == "__main__":
    unittest.main()
