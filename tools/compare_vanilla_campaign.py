"""Read-only stopped-game comparison against the first protective backup.

Campaign bytes/file sets only: unchanged does not verify vanilla menu selection.
PROFILE/settings are not decoded. No restoration, launch or source discovery.
"""
import argparse
import contextlib
import json
from pathlib import PurePosixPath
import re
import stat
import sys

import prepare_vanilla_backup as protection


CAMPAIGN = re.compile(r"(?:GAME|DLC1|DLC2)-AUTOSAVE(?:[0-9]|1[01])", re.IGNORECASE)
AP_ROOT = re.compile(r"ap-[0-9a-f]{40}", re.IGNORECASE)


def is_vanilla_campaign(relative):
    parts = PurePosixPath(relative).parts
    return not any(AP_ROOT.fullmatch(part) for part in parts) and any(CAMPAIGN.fullmatch(part) for part in parts)


def compare(args):
    sources = {"steam_app": protection.explicit_path(args.steam_app_root),
               "local_provider": protection.explicit_path(args.local_provider_root)}
    destination = protection.explicit_path(args.backup_directory)
    identity = protection.expected_identity(args, sources)
    protection.require_stopped()
    with contextlib.ExitStack() as stack:
        directories = protection.pin_ancestors(stack, [*sources.values(), destination])
        entries = protection.verify_backup(destination, identity)
        baseline = {(entry["origin"], entry["relative"]): (entry["bytes"], entry["sha256"])
                    for entry in entries if is_vanilla_campaign(entry["relative"])}
        if not baseline:
            raise protection.Refused("backup contains no vanilla campaign files; comparison is inconclusive")
        before = protection.inventory(sources)
        handles = {}
        for (label, relative), info in before.items():
            source = sources[label] / relative
            if stat.S_ISDIR(info[2]):
                if source not in directories:
                    stack.enter_context(protection.pinned(source, directory=True))
                    directories.add(source)
            elif is_vanilla_campaign(relative):
                handles[(label, relative)] = stack.enter_context(protection.pinned(source))
        if before != protection.inventory(sources):
            raise protection.Refused("source changed while establishing campaign comparison")
        current = {key: protection.digest(stream) for key, stream in handles.items()}
        if before != protection.inventory(sources):
            raise protection.Refused("source changed during campaign comparison")
        protection.require_stopped()
        added = len(current.keys() - baseline.keys())
        removed = len(baseline.keys() - current.keys())
        modified = sum(current[key] != baseline[key] for key in current.keys() & baseline.keys())
        return {"result": "vanilla_campaign_changed" if added or removed or modified else "vanilla_campaign_unchanged",
                "baseline_campaign_files": len(baseline), "current_campaign_files": len(current),
                "added": added, "removed": removed, "modified": modified,
                "selection_verification": "manual_vanilla_menu_check_required"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steam-account", required=True)
    parser.add_argument("--steam-app-root", required=True)
    parser.add_argument("--local-provider-root", required=True)
    parser.add_argument("--backup-directory", required=True)
    args = parser.parse_args(argv)
    try:
        result = compare(args)
        print(json.dumps(result))
        return 0 if result["result"] == "vanilla_campaign_unchanged" else 1
    except (protection.Refused, OSError, ValueError, KeyError, TypeError, RecursionError) as error:
        reason = str(error) if isinstance(error, protection.Refused) else "inaccessible or malformed comparison data"
        print(json.dumps({"result": "vanilla_comparison_refused", "reason": reason}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
