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


class Refused(Exception):
    pass


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
        raise Refused("hard-linked files are not supported")
    return info


@contextlib.contextmanager
def pinned(path, directory=False):
    """Retain no-write/no-delete handles; regular file streams are exclusive."""
    import msvcrt
    api = kernel()
    flags = 0x00200000 | (0x02000000 if directory else 0x08000000)
    handle = api.CreateFileW(str(path), 0x80 if directory else 0x80000000,
                             1 if directory else 0, None, 3, flags, None)
    if handle == ctypes.c_void_p(-1).value:
        raise Refused("cannot pin a path exclusively; close all source readers/writers")
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
                if os.fstat(source.fileno()).st_nlink != 1:
                    raise Refused("hard-linked files are not supported")
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
                                         info.st_size, info.st_mtime_ns)
            if stat.S_ISDIR(info.st_mode):
                for child in sorted(path.iterdir()):
                    visit(child)
        visit(root)
    return result


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
            if digest(stream) != (entry["bytes"], entry["sha256"]):
                raise Refused("backup integrity verification failed")
    actual_files = {p for (_, p), info in tree.items() if stat.S_ISREG(info[2])}
    actual_dirs = {p for (_, p), info in tree.items() if stat.S_ISDIR(info[2])}
    if actual_files != expected_files or actual_dirs != set(directories) | {"."}:
        raise Refused("backup file/directory set differs from manifest")
    if tree != inventory({"backup": destination}):
        raise Refused("backup changed during integrity verification")
    return entries


def protect(args):
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
    identity = expected_identity(args, sources)
    with contextlib.ExitStack() as stack:
        if args.action == "verify":
            pin_ancestors(stack, [destination])
            entries = verify_backup(destination, identity)
            return {"result": "protective_backup_verified", "files": len(entries)}
        require_stopped()
        pinned_dirs = pin_ancestors(stack, [*sources.values(), destination.parent])
        if not (steam_root / "remote").is_dir():
            raise Refused("explicit Steam origin lacks the required remote directory")
        before = inventory(sources)
        handles = {}
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
        if not any(label == "steam_app" and relative.startswith("remote/") for label, relative in handles):
            raise Refused("Steam remote has no original files; explicit investigation required")
        if before != inventory(sources):
            raise Refused("source changed while establishing exclusive protection")
        if destination.exists():
            pin_ancestors(stack, [destination])
            entries = verify_backup(destination, identity)
            old = {(e["origin"], e["relative"]): (e["bytes"], e["sha256"]) for e in entries}
            current = {key: digest(source) for key, source in handles.items()}
            if old != current:
                raise Refused("originals differ from preserved first backup; no overwrite or activation")
        else:
            destination.mkdir()  # Create-only; never resume an interrupted directory.
            stack.enter_context(pinned(destination, directory=True))
            for relative in directories:
                (destination / relative).mkdir(parents=True, exist_ok=True)
            entries = []
            for (label, relative), source in handles.items():
                size, sha = copy_file(source, destination / label / relative)
                entries.append({"origin": label, "relative": relative, "bytes": size, "sha256": sha})
            if before != inventory(sources):
                raise Refused("source changed during backup; partial protection retained")
            require_stopped()
            manifest = {**identity, "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                        "directories": sorted(directories), "files": entries}
            with (destination / MANIFEST).open("x", encoding="utf-8", newline="\n") as stream:
                json.dump(manifest, stream, indent=2, sort_keys=True)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            verify_backup(destination, identity)
        if before != inventory(sources):
            raise Refused("original source inventory changed; activation refused")
        require_stopped()
        return {"result": "protective_backup_ready", "files": len(entries)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "verify"))
    parser.add_argument("--steam-account", required=True)
    parser.add_argument("--steam-app-root", required=True)
    parser.add_argument("--local-provider-root", required=True)
    parser.add_argument("--backup-directory", required=True)
    parser.add_argument("--ap-root", required=True)
    parser.add_argument("--uninstall-root", action="append", required=True)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(protect(args)))
        return 0
    except (Refused, OSError, ValueError, KeyError, TypeError, RecursionError) as error:
        # Do not print OS exceptions: they can include private source filenames.
        reason = str(error) if isinstance(error, Refused) else "incomplete/inaccessible or malformed protection data"
        print(json.dumps({"result": "protection_refused", "reason": reason}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
