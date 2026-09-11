"""Read-only stopped-game comparison against an exact run protection reference.

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
    args.operation_stage = "comparison_source_identity"
    sources = {"steam_app": protection.explicit_path(args.steam_app_root),
               "local_provider": protection.explicit_path(args.local_provider_root)}
    destination = protection.explicit_path(args.backup_directory)
    identity = protection.expected_identity(args, sources)
    args.operation_stage = "comparison_stopped_precondition"
    protection.require_stopped()
    with contextlib.ExitStack() as stack:
        directories = protection.pin_ancestors(stack, [*sources.values(), destination])
        args.operation_stage = "comparison_reference_integrity"
        entries = protection.verify_backup(destination, identity)
        baseline = {(entry["origin"], entry["relative"]): (entry["bytes"], entry["sha256"])
                    for entry in entries if is_vanilla_campaign(entry["relative"])}
        if not baseline:
            raise protection.Refused("backup contains no vanilla campaign files; comparison is inconclusive")
        args.operation_stage = "comparison_inventory"
        before = protection.inventory(sources)
        handles = {}
        acquired = {}
        for (label, relative), info in before.items():
            source = sources[label] / relative
            if stat.S_ISDIR(info[2]):
                if source not in directories:
                    stack.enter_context(protection.pinned(source, directory=True))
                    directories.add(source)
            elif is_vanilla_campaign(relative):
                handles[(label, relative)] = stack.enter_context(protection.pinned(source))
                acquired[(label, relative)] = protection.handle_metadata(handles[(label, relative)])
        protection.inventory_guard(sources, before, handles, acquired, "comparison_acquisition", "source changed while establishing campaign comparison")
        current = {key: protection.digest(stream) for key, stream in handles.items()}
        protection.inventory_guard(sources, before, handles, acquired, "comparison_hash", "source changed during campaign comparison")
        protection.require_stopped()
        added = len(current.keys() - baseline.keys())
        removed = len(baseline.keys() - current.keys())
        modified = sum(current[key] != baseline[key] for key in current.keys() & baseline.keys())
        detail, rows = protection.differences([e for e in entries if is_vanilla_campaign(e["relative"])], current, before, "run_campaign_reference")
        args.private_comparison = {"summary": detail, "differences": rows}
        return {"result": "vanilla_campaign_changed" if added or removed or modified else "vanilla_campaign_unchanged",
                "baseline_campaign_files": len(baseline), "current_campaign_files": len(current),
                "added": added, "removed": removed, "modified": modified, "comparison": detail,
                "selection_verification": "manual_vanilla_menu_check_required"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steam-account", required=True)
    parser.add_argument("--steam-app-root", required=True)
    parser.add_argument("--local-provider-root", required=True)
    parser.add_argument("--backup-directory", required=True)
    parser.add_argument("--diagnostic-file")
    args = parser.parse_args(argv)
    diagnostic = None
    pins = contextlib.ExitStack()
    try:
        if args.diagnostic_file:
            path = protection.explicit_path(args.diagnostic_file)
            for root in (args.steam_app_root, args.local_provider_root, args.backup_directory):
                if protection.overlap(path, protection.explicit_path(root)):
                    raise protection.Refused("comparison diagnostic overlaps protected input", stage="comparison_diagnostic_creation")
            args.operation_stage = "comparison_diagnostic_creation"
            protection.pin_ancestors(pins, [path.parent])
            diagnostic = pins.enter_context(path.open("x", encoding="utf-8"))
        result = compare(args)
        if diagnostic: json.dump(args.private_comparison, diagnostic, indent=2)
        print(json.dumps(result))
        return 0 if result["result"] == "vanilla_campaign_unchanged" else 1
    except (protection.Refused, OSError, ValueError, KeyError, TypeError, RecursionError) as error:
        if isinstance(error, protection.Refused) and error.stage == "protection":
            error.stage = getattr(args, "operation_stage", "comparison_diagnostic_creation")
        failure = protection.failure_record(error, getattr(args, "operation_stage", "campaign_comparison"))
        failure.update(result="vanilla_comparison_refused", operation="campaign_comparison")
        if diagnostic:
            json.dump({**failure, "original_error": str(error), "metadata": getattr(error, "private_metadata", None)}, diagnostic)
        print(json.dumps(failure), file=sys.stderr)
        return 1
    finally:
        pins.close()


if __name__ == "__main__":
    sys.exit(main())
