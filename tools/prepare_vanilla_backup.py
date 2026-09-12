#!/usr/bin/env python3
"""Stopped-game Windows/Steam original-save protection; never launches or restores.

The create-only completion manifest is published after copying and readback. A
partial directory is retained and refused. This is local protection, not a Steam
Cloud snapshot or a replacement for native AP-session admission checks.
"""

import argparse
import contextlib
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import uuid


class Refused(Exception):
    def __init__(self, reason, *, stage="protection", distinction=None, comparison=None, private=None, win32_error=None):
        super().__init__(reason)
        self.stage, self.distinction = stage, distinction or reason
        self.comparison, self.private_metadata = comparison, private
        self.win32_error = win32_error


def failure_record(error, stage="protection"):
    return {"result": "protection_refused", "operation": "protection", "stage": getattr(error, "stage", stage),
            "reason": str(error) if isinstance(error, Refused) else type(error).__name__,
            "distinction": getattr(error, "distinction", None) or "OS_or_data_error_see_private_diagnostic",
            "win32_error": getattr(error, "win32_error", None) or getattr(error, "winerror", None),
            "errno": getattr(error, "errno", None), "comparison": getattr(error, "comparison", None),
            "metadata_summary": getattr(error, "summary", None)}


MANIFEST = "vanilla-protection.json"
SCHEMA = "sentinel-vanilla-protection-v1"
BLOCKED_PROCESSES = {"doometernalx64vk.exe", "doometernal.exe", "steam.exe",
                     "steamwebhelper.exe"}


def kernel():
    if os.name != "nt":
        raise Refused("supported preparation requires Windows")
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                               wintypes.HANDLE]
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.GetFileInformationByHandleEx.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                               ctypes.c_void_p, wintypes.DWORD]
    api.GetDriveTypeW.argtypes = [wintypes.LPCWSTR]
    return api


def require_stopped():
    """Fail closed when process enumeration fails; no shell or process launch."""
    class ProcessEntry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]
    api = kernel()
    api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    for method in (api.Process32FirstW, api.Process32NextW):
        method.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    handle = api.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise Refused("cannot establish stopped-game/Steam precondition")
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        ok = api.Process32FirstW(handle, ctypes.byref(entry))
        found = set()
        while ok:
            if entry.szExeFile.casefold() in BLOCKED_PROCESSES:
                found.add(entry.szExeFile.casefold())
            ok = api.Process32NextW(handle, ctypes.byref(entry))
        if ctypes.get_last_error() != 18:  # ERROR_NO_MORE_FILES
            raise Refused("cannot complete process enumeration")
        if found:
            raise Refused("exit DOOM and Steam completely before preparation")
    finally:
        api.CloseHandle(handle)


def explicit_path(value):
    # Do not normalize aliases/traversal into an apparently safe destination.
    path = Path(value)
    if (not re.match(r"^[A-Za-z]:[\\/]", value) or
            any(p in (".", "..") or p.endswith((".", " ")) or ":" in p
                for p in re.split(r"[\\/]", value[3:]) if p)):
        raise Refused("paths must be explicit local drive-absolute paths without aliases")
    if kernel().GetDriveTypeW(path.anchor) != 3:
        raise Refused("sources and protection require local fixed drives")
    return path


def overlap(left, right):
    return left == right or left in right.parents or right in left.parents


def check_node(path):
    info = path.lstat()
    if info.st_file_attributes & 0x400:  # FILE_ATTRIBUTE_REPARSE_POINT
        raise Refused("reparse points are not supported")
    if not (stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode)):
        raise Refused("non-regular source entry")
    if stat.S_ISREG(info.st_mode) and info.st_nlink != 1:
        # Windows path stat may report an unavailable link count. Establish it
        # using file metadata, never infer a hard link from zero/unknown.
        api = kernel()
        handle = api.CreateFileW(str(path), 0x80, 7, None, 3, 0x00200000, None)
        if handle == ctypes.c_void_p(-1).value:
            raise Refused("file link metadata unavailable", distinction="path_link_count_unverified",
                win32_error=ctypes.get_last_error(), private={"path": str(path), "operation": "path_link_attributes_open", "raw_path_nlink": info.st_nlink})
        try:
            verify_links(api, handle, path, "path_link_metadata", info.st_nlink)
        finally:
            api.CloseHandle(handle)
    return info


def verify_links(api, handle, path, operation, raw_count):
    class Standard(ctypes.Structure):
        _fields_ = [("allocation", ctypes.c_int64), ("size", ctypes.c_int64), ("links", wintypes.DWORD),
                    ("delete_pending", wintypes.BOOLEAN), ("directory", wintypes.BOOLEAN)]
    value = Standard()
    private = {"path": str(path), "operation": operation, "raw_stat_nlink": raw_count}
    if not api.GetFileInformationByHandleEx(handle, 1, ctypes.byref(value), ctypes.sizeof(value)):
        raise Refused("file link metadata unavailable", distinction="handle_link_query_failed",
                      win32_error=ctypes.get_last_error(), private=private)
    private.update(handle_nlink=value.links, delete_pending=bool(value.delete_pending))
    if value.links > 1:
        raise Refused("hard-linked files are not supported", distinction="confirmed_multiple_file_links", private=private)
    if value.links != 1 or value.delete_pending or value.directory:
        raise Refused("file link metadata is not a stable single file", distinction="handle_link_state_unverified", private=private)
    if raw_count not in (0, 1):
        raise Refused("path and handle link metadata disagree", distinction="link_metadata_changed", private=private)


@contextlib.contextmanager
def pinned(path, directory=False):
    """Retain no-write/no-delete handles; regular file streams are exclusive."""
    import msvcrt
    api = kernel()
    flags = 0x00200000 | (0x02000000 if directory else 0x08000000)
    handle = api.CreateFileW(str(path), 0x80 if directory else 0x80000000,
                             1 if directory else 0, None, 3, flags, None)
    if handle == ctypes.c_void_p(-1).value:
        raise Refused("cannot pin a path exclusively; close all source readers/writers",
                      stage="exclusive_acquisition", win32_error=ctypes.get_last_error(), private={"path": str(path)})
    try:
        attrs = ctypes.create_string_buffer(8)
        if (not api.GetFileInformationByHandleEx(handle, 9, attrs, len(attrs)) or
                int.from_bytes(attrs.raw[:4], "little") & 0x400):
            raise Refused("cannot establish ordinary pinned path")
        streams = ctypes.create_string_buffer(65536)
        if (not api.GetFileInformationByHandleEx(handle, 7, streams, len(streams)) and
                not (directory and ctypes.get_last_error() == 38)):  # No directory streams.
            raise Refused("cannot establish complete file stream set")
        offset = 0
        while True:
            nxt = int.from_bytes(streams.raw[offset:offset + 4], "little")
            length = int.from_bytes(streams.raw[offset + 4:offset + 8], "little")
            name = streams.raw[offset + 24:offset + 24 + length].decode("utf-16-le")
            if name not in ("", "::$DATA"):
                raise Refused("named data streams require separate explicit protection")
            if not nxt:
                break
            offset += nxt
        if directory:
            yield None
        else:
            fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
            handle = None  # fd owns the Windows handle now.
            with os.fdopen(fd, "rb") as source:
                verify_links(api, msvcrt.get_osfhandle(source.fileno()), path, "exclusive_handle_link_metadata",
                             os.fstat(source.fileno()).st_nlink)
                yield source
    finally:
        if handle is not None:
            api.CloseHandle(handle)


def pin_ancestors(stack, paths):
    seen = set()
    for path in paths:
        for directory in reversed((path, *path.parents)):
            if directory in seen:
                continue
            check_node(directory)
            if not directory.is_dir():
                raise Refused("expected an existing directory")
            stack.enter_context(pinned(directory, directory=True))
            seen.add(directory)
    return seen


def inventory(sources):
    result = {}
    for label, root in sources.items():
        def visit(path):
            info = check_node(path)
            relative = path.relative_to(root).as_posix()
            result[(label, relative)] = (info.st_ino, info.st_dev, info.st_mode,
                                         0 if stat.S_ISDIR(info.st_mode) else info.st_size, info.st_mtime_ns)
            if stat.S_ISDIR(info.st_mode):
                for child in sorted(path.iterdir()):
                    visit(child)
        visit(root)
    return result


METADATA_FIELDS = ("identity", "volume", "mode", "size", "mtime_ns")


def handle_metadata(source):
    info = os.fstat(source.fileno())
    return (info.st_ino, info.st_dev, info.st_mode, info.st_size, info.st_mtime_ns)


def inventory_guard(sources, before, handles, acquired, stage, reason):
    # Path enumeration still owns membership/path identity. A held handle alone
    # cannot establish that a name still refers to the original object.
    after = inventory(sources)
    handle_after = {key: handle_metadata(source) for key, source in handles.items()}
    changed = before.keys() ^ after.keys()
    changed |= {key for key in before.keys() & after.keys() if before[key] != after[key]}
    changed_handles = {key for key in acquired if acquired[key] != handle_after[key]}
    def comparable(value):
        # Windows path stat infers execute bits from .exe/.bat/.cmd; fstat cannot.
        return value[:2] + (value[2] & ~0o111,) + value[3:]
    handle_path_mismatches = {key for key in acquired if comparable(before[key]) != comparable(acquired[key])}
    if not changed and not changed_handles and not handle_path_mismatches:
        return
    rows, fields = [], {}
    for key in sorted(changed | changed_handles | handle_path_mismatches):
        old, new = before.get(key), after.get(key)
        delta = [field for field, a, b in zip(METADATA_FIELDS, old, new) if a != b] if old and new else ["presence"]
        if key in acquired:
            for left, right in ((old, acquired[key]), (acquired[key], handle_after[key])):
                if left is not None:
                    delta += [field for field, a, b in zip(METADATA_FIELDS, comparable(left), comparable(right)) if a != b]
        delta = sorted(set(delta))
        for field in delta:
            fields[field] = fields.get(field, 0) + 1
        def values(value):
            return dict(zip(METADATA_FIELDS, value)) if value is not None else None
        rows.append({"origin": key[0], "entry": key[1], "path_before": values(old),
                     "path_after": values(new), "handle_acquired": values(acquired.get(key)),
                     "handle_after": values(handle_after.get(key)), "changed_fields": delta})
    error = Refused(reason)
    error.stage = stage
    error.summary = {"changed_entries": len(rows), "changed_fields": fields,
                     "handle_changed_entries": len(changed_handles), "handle_path_mismatches": len(handle_path_mismatches)}
    error.private_metadata = {"origins": {label: str(path) for label, path in sources.items()}, "differences": rows}
    raise error


def digest(stream):
    stream.seek(0)
    value = hashlib.sha256()
    size = 0
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        value.update(chunk)
        size += len(chunk)
    return size, value.hexdigest()


def copy_file(source, destination):
    source.seek(0)
    with destination.open("xb") as output:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            output.write(chunk)
        output.flush()
        os.fsync(output.fileno())
    with destination.open("rb") as output:
        actual = digest(output)
    expected = digest(source)
    if actual != expected:
        raise Refused("backup readback differs from pinned original")
    return actual


def expected_identity(args, sources):
    return {"schema": SCHEMA, "provider": "Steam/ISteamRemoteStorage014",
            "steam_account_id": args.steam_account,
            "origins": {label: str(path) for label, path in sources.items()}}


CAMPAIGN = re.compile(r"(?:GAME|DLC1|DLC2)-AUTOSAVE(?:[0-9]|1[01])", re.I)
AP_ROOT = re.compile(r"ap-[0-9a-f]{40}", re.I)


def is_campaign(relative):
    parts = relative.split("/")
    return not any(AP_ROOT.fullmatch(p) for p in parts) and any(CAMPAIGN.fullmatch(p) for p in parts)


def differences(entries, current, metadata=None, affected="historical_backup"):
    old = {(e["origin"], e["relative"]): e for e in entries}
    categories = ("added", "removed", "content_changed", "metadata_only", "unchanged", "metadata_unavailable")
    counts = dict.fromkeys(categories, 0)
    campaigns = dict.fromkeys(categories, 0)
    rows = []
    for key in sorted(old.keys() | current.keys()):
        previous = old.get(key)
        now = current.get(key)
        prior = (previous["bytes"], previous["sha256"]) if previous else None
        changed_metadata = {}
        if previous and now and metadata and previous.get("source_metadata"):
            changed_metadata = {name: {"before": previous["source_metadata"][name], "after": value}
                                for name, value in zip(METADATA_FIELDS, metadata[key])
                                if previous["source_metadata"].get(name) != value}
        kind = ("added" if not previous else "removed" if now is None else "content_changed" if prior != now
                else "metadata_only" if changed_metadata else "unchanged" if previous.get("source_metadata") else "metadata_unavailable")
        counts[kind] += 1
        if is_campaign(key[1]): campaigns[kind] += 1
        if kind not in ("unchanged", "metadata_unavailable"):
            rows.append({"origin": key[0], "relative": key[1], "category": kind, "campaign": is_campaign(key[1]),
                         "before_bytes_sha256": prior, "after_bytes_sha256": now, "metadata": changed_metadata})
    return {"affected": affected, "counts": counts, "campaign_counts": campaigns}, rows


def verify_backup(destination, identity):
    # Inspect the complete tree, including unused entries, before trusting paths.
    tree = inventory({"backup": destination})
    if not (destination / MANIFEST).is_file():
        raise Refused("incomplete protection directory; retained without repair or activation")
    with pinned(destination / MANIFEST) as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict) or any(manifest.get(key) != value for key, value in identity.items()):
        raise Refused("completed backup belongs to a different origin/account")
    entries = manifest.get("files")
    directories = manifest.get("directories")
    if not isinstance(entries, list) or not entries or not isinstance(directories, list):
        raise Refused("invalid completion manifest")
    expected_files = {MANIFEST}
    for entry in entries:
        if not isinstance(entry, dict):
            raise Refused("invalid manifest file entry")
        label, relative = entry["origin"], entry["relative"]
        if (label not in identity["origins"] or not isinstance(relative, str) or
                any(part in ("", ".", "..") for part in relative.split("/")) or
                "\\" in relative or ":" in relative):
            raise Refused("invalid manifest source path")
        target = f"{label}/{relative}"
        if target in expected_files:
            raise Refused("duplicate manifest path")
        expected_files.add(target)
        with pinned(destination / label / relative) as stream:
            actual = digest(stream)
            if actual != (entry["bytes"], entry["sha256"]):
                raise Refused("backup integrity verification failed", distinction="preserved_backup_content_corruption",
                    comparison={"affected": "backup_integrity", "content_changed": 1},
                    private={"origin": label, "relative": relative, "expected_bytes_sha256": [entry["bytes"], entry["sha256"]],
                             "actual_bytes_sha256": actual})
    actual_files = {p for (_, p), info in tree.items() if stat.S_ISREG(info[2])}
    actual_dirs = {p for (_, p), info in tree.items() if stat.S_ISDIR(info[2])}
    if actual_files != expected_files or actual_dirs != set(directories) | {"."}:
        raise Refused("backup file/directory set differs from manifest", distinction="backup_entry_set_mismatch",
            comparison={"affected": "backup_integrity", "added": len(actual_files-expected_files), "removed": len(expected_files-actual_files)},
            private={"added_files": sorted(actual_files-expected_files), "removed_files": sorted(expected_files-actual_files),
                     "added_directories": sorted(actual_dirs-set(directories)-{"."}), "removed_directories": sorted(set(directories)-actual_dirs)})
    if tree != inventory({"backup": destination}):
        raise Refused("backup changed during integrity verification")
    return entries


def protect(args):
    stage = "source_identity"
    try:
        return _protect(args)
    except (Refused, OSError, ValueError, KeyError, TypeError) as error:
        if not isinstance(error, Refused):
            raise Refused(type(error).__name__, stage=getattr(args, "operation_stage", stage),
                          distinction="OS_or_malformed_data_error", private={"os_error": str(error)},
                          win32_error=getattr(error, "winerror", None)) from error
        if error.stage == "protection": error.stage = getattr(args, "operation_stage", stage)
        raise


def _protect(args):
    if not re.fullmatch(r"[1-9][0-9]*", args.steam_account) or int(args.steam_account) > 0xffffffff:
        raise Refused("Steam account must be the explicit numeric userdata account ID")
    sources = {"steam_app": explicit_path(args.steam_app_root),
               "local_provider": explicit_path(args.local_provider_root)}
    steam_root = sources["steam_app"]
    if tuple(p.casefold() for p in steam_root.parts[-3:]) != ("userdata", args.steam_account, "782330"):
        raise Refused("Steam origin must end in userdata/account-id/782330")
    destination = explicit_path(args.backup_directory)
    excluded = [explicit_path(args.ap_root), *(explicit_path(p) for p in args.uninstall_root)]
    if any(overlap(destination, p) for p in [*sources.values(), *excluded]):
        raise Refused("backup must be separate from original, AP and uninstall roots")
    if overlap(*sources.values()):
        raise Refused("source roots must not overlap")
    run_parent = getattr(args, 'run_backup_parent', None)
    if run_parent:
        run_parent = explicit_path(run_parent)
        if any(overlap(run_parent, p) for p in [*sources.values(), *excluded, destination]):
            raise Refused("run protection parent must be separate from source, historical backup, AP and uninstall roots")
        if not destination.is_dir():
            raise Refused("run protection parent requires the existing historical backup")
    identity = expected_identity(args, sources)
    with contextlib.ExitStack() as stack:
        if args.action == "verify":
            pin_ancestors(stack, [destination])
            entries = verify_backup(destination, identity)
            return {"result": "protective_backup_verified", "files": len(entries)}
        args.operation_stage = "stopped_precondition"
        require_stopped()
        pinned_dirs = pin_ancestors(stack, [*sources.values(), destination.parent])
        if run_parent:
            pin_ancestors(stack, [run_parent])
        if not (steam_root / "remote").is_dir():
            raise Refused("explicit Steam origin lacks the required remote directory")
        args.operation_stage = "source_inventory"
        before = inventory(sources)
        handles = {}
        acquired = {}
        directories = []
        for (label, relative), info in before.items():
            source = sources[label] / relative
            if stat.S_ISDIR(info[2]):
                directories.append(label if relative == "." else f"{label}/{relative}")
                if source not in pinned_dirs:
                    stack.enter_context(pinned(source, directory=True))
                    pinned_dirs.add(source)
            else:
                handles[(label, relative)] = stack.enter_context(pinned(source))
                acquired[(label, relative)] = handle_metadata(handles[(label, relative)])
        if not any(label == "steam_app" and relative.startswith("remote/") for label, relative in handles):
            raise Refused("Steam remote has no original files; explicit investigation required")
        inventory_guard(sources, before, handles, acquired, "inventory_acquisition",
                        "source changed while establishing exclusive protection")
        current = {key: digest(source) for key, source in handles.items()}
        inventory_guard(sources, before, handles, acquired, "inventory_hash", "source changed during hashing")
        historical = None
        reused = False
        if destination.exists():
            args.operation_stage = "historical_integrity"
            pin_ancestors(stack, [destination])
            entries = verify_backup(destination, identity)
            historical, rows = differences(entries, current, before)
            args.comparisons = [{"summary": historical, "differences": rows}]
            reused = not any(historical["counts"][c] for c in ("added", "removed", "content_changed", "metadata_only", "metadata_unavailable"))
        # The first backup is recovery history, not a permanently frozen source.
        # The caller owns recovery of prior test outcomes before requesting a new reference.
        reference = getattr(args, "reference_directory", None)
        if reference:
            reference = explicit_path(reference)
            if reference.parent not in (destination.parent, run_parent):
                raise Refused("run reference must use the configured private backup parent", stage="reference_identity")
            if reference != destination:
                args.operation_stage = "reference_integrity"
                pin_ancestors(stack, [reference])
                candidate_entries = verify_backup(reference, identity)
                comparison, rows = differences(candidate_entries, current, before, "previous_run_reference")
                args.comparisons = getattr(args, "comparisons", []) + [{"summary": comparison, "differences": rows}]
                if not any(comparison["counts"][c] for c in ("added", "removed", "content_changed", "metadata_only", "metadata_unavailable")):
                    destination, entries, reused = reference, candidate_entries, True
        if run_parent and destination.parent != run_parent:
            reused = False
        if destination.exists() and not reused:
            destination = (run_parent or destination.parent) / (destination.name + "-run-" + uuid.uuid4().hex)
        if not reused:
            args.operation_stage = "snapshot_copy"
            destination.mkdir()  # Create-only; never resume an interrupted directory.
            stack.enter_context(pinned(destination, directory=True))
            for relative in directories:
                (destination / relative).mkdir(parents=True, exist_ok=True)
            entries = []
            for (label, relative), source in handles.items():
                size, sha = copy_file(source, destination / label / relative)
                if (size, sha) != current[(label, relative)]:
                    raise Refused("source content changed during copy", stage="snapshot_copy", distinction="concurrent_mutation")
                entries.append({"origin": label, "relative": relative, "bytes": size, "sha256": sha,
                                "source_metadata": dict(zip(METADATA_FIELDS, before[(label, relative)]))})
            inventory_guard(sources, before, handles, acquired, "inventory_copy",
                            "source changed during backup; partial protection retained")
            require_stopped()
            manifest = {**identity, "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                        "directories": sorted(directories), "files": entries}
            with (destination / MANIFEST).open("x", encoding="utf-8", newline="\n") as stream:
                json.dump(manifest, stream, indent=2, sort_keys=True)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            verify_backup(destination, identity)
        inventory_guard(sources, before, handles, acquired, "inventory_final",
                        "original source inventory changed; activation refused")
        require_stopped()
        return {"result": "protective_backup_ready", "files": len(entries), "reference_directory": str(destination),
                "reference_manifest_sha256": hashlib.sha256((destination / MANIFEST).read_bytes()).hexdigest(),
                "reused": reused, "historical_comparison": historical}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "verify"))
    parser.add_argument("--steam-account", required=True)
    parser.add_argument("--steam-app-root", required=True)
    parser.add_argument("--local-provider-root", required=True)
    parser.add_argument("--backup-directory", required=True)
    parser.add_argument("--ap-root", required=True)
    parser.add_argument("--uninstall-root", action="append", required=True)
    parser.add_argument("--diagnostic-file", help="Create-only private diagnostic outside original/backup/AP roots")
    parser.add_argument("--reference-directory", help="Exact previously completed reference; never inferred by timestamp")
    parser.add_argument("--run-backup-parent", help="Existing separate local parent for new run snapshots; historical backup remains unchanged")
    args = parser.parse_args(argv)
    diagnostic = None
    diagnostic_pins = contextlib.ExitStack()
    try:
        if args.diagnostic_file:
            path = explicit_path(args.diagnostic_file)
            if any(overlap(path, explicit_path(root)) for root in
                   (args.steam_app_root, args.local_provider_root, args.backup_directory, args.ap_root)):
                raise Refused("private diagnostic must be outside original, backup and AP roots")
            # Pin/check ancestors so aliases cannot redirect the diagnostic into originals.
            pin_ancestors(diagnostic_pins, [path.parent])
            diagnostic = path.open("x", encoding="utf-8")
        result = protect(args)
        print(json.dumps(result))
        if diagnostic:
            json.dump({"result": "protection_completed", "receipt": result, "comparisons": getattr(args, "comparisons", [])}, diagnostic)
        return 0
    except (Refused, OSError, ValueError, KeyError, TypeError, RecursionError) as error:
        # Do not print OS exceptions: they can include private source filenames.
        failure = failure_record(error, getattr(args, "operation_stage", "protection"))
        if diagnostic:
            json.dump({**failure, "metadata": getattr(error, "private_metadata", None),
                       "comparisons": getattr(args, "comparisons", []), "original_error": str(error)}, diagnostic, indent=2)
        print(json.dumps(failure), file=sys.stderr)
        return 1
    finally:
        if diagnostic:
            diagnostic.close()
        diagnostic_pins.close()


if __name__ == "__main__":
    sys.exit(main())
