"""Build immutable Core runtime assets from qualified Windows Release outputs."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI = {"base":1,"wire":1,"engine":1,"context":1,"native":1,"save":1,"admission":1,"backup":1,"installation":1,"weapon_points":1,"campaign_menu":1,"inventory":5,"arsenal":1,"runes":1,"special":1,"deathlink":1,"automap":1,"commands":1}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def pe64(data):
    offset = struct.unpack_from("<I", data, 60)[0]
    if data[:2] != b"MZ" or data[offset:offset+4] != b"PE\0\0" or struct.unpack_from("<H", data, offset+4)[0] != 0x8664:
        raise ValueError("Production artifact must be Windows x64")

def build(bin_dir, output, commit, dirty):
    if not re.fullmatch("[a-f0-9]{40}", commit):
        raise ValueError("Supply the full source commit")
    spec = json.loads((ROOT / "version.json").read_text())
    base = ".".join(str(spec[key]) for key in ("major", "minor", "patch"))
    channel, rc = spec["channel"], spec["rc_number"]
    if type(rc) is not int or (channel == "rc" and rc < 1) or (channel == "stable" and rc != 0) or channel not in {"rc", "stable"}:
        raise ValueError("Invalid canonical version")
    version = base + (f"-rc-{rc}" if channel == "rc" else "")
    files = {name: (bin_dir / name).read_bytes() for name in ("sentinel_core.dll", "msimg32.dll", "sentinel_probe.exe")}
    for data in files.values():
        pe64(data)
    identities = set(re.findall(re.escape(version.encode().ljust(32, b"\0")) + rb"([a-f0-9]{64})\0", files["sentinel_core.dll"]))
    if len(identities) != 1:
        raise ValueError("Core binary version/build identity does not match canonical version")
    build_id = identities.pop().decode()
    files["prepare_vanilla_backup.py"] = (ROOT / "tools/prepare_vanilla_backup.py").read_bytes()
    for name, source in {
        "LICENSE.txt": ROOT / "LICENSE", "MinHook-LICENSE.txt": ROOT / "third_party/minhook/LICENSE.txt",
        "MinHook-NOTICE.txt": ROOT / "third_party/minhook/NOTICE",
    }.items():
        files["notices/" + name] = source.read_bytes()
    output.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        destination = output / name
        if destination.exists() and destination.read_bytes() != data:
            raise ValueError(f"Immutable candidate collision: {name}")
    archive_path = output / "sentinel-runtime.zip"
    temporary = output / "sentinel-runtime.zip.incoming"
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    archive_data = temporary.read_bytes()
    if archive_path.exists() and archive_path.read_bytes() != archive_data:
        temporary.unlink()
        raise ValueError("Immutable runtime ZIP collision")
    temporary.replace(archive_path)
    files["sentinel-runtime.zip"] = archive_data
    value = {
        "schema": 1, "product": "sentinel-core", "version": version, "base_version": base,
        "channel": channel, "rc_number": rc, "source_commit": commit, "source_dirty": dirty,
        "build_id": build_id, "pair_id": build_id, "mod_versions": ["0.6.0"],
        "platform": "windows", "architecture": "x64", "abi": ABI, "required_capabilities": [2097152],
        "qualified_game": {"image_size": 0x7431000, "timestamp": 0x6a7b9b8c, "entry_rva": 0x286caa8},
        "toolchain": {"msvc": "14.44.35207", "crt": "static", "configuration": "Release"},
        "dependencies": ["MinHook 1.3.4", "HDE64", "Windows system APIs"],
        "probe_policy": "Identity/ABI first; gameplay admission and observations require the live qualified game.",
        "evidence": {"synthetic": "see evidence report paired by build_id", "windows_gameplay": "pending", "proton_gameplay": "pending"},
        "artifacts": [{"path": name, "role": "runtime_zip" if name.endswith(".zip") else
            "notice" if name.startswith("notices/") else "bootstrap" if name == "msimg32.dll" else
            "probe" if name.endswith(".exe") else "helper" if name.endswith(".py") else "core", "size": len(data), "sha256": digest(data)}
            for name, data in sorted(files.items())],
    }
    for name, data in files.items():
        if name != "sentinel-runtime.zip":
            destination = output / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    with zipfile.ZipFile(archive_path) as archive:
        if set(archive.namelist()) != set(files) - {"sentinel-runtime.zip"}:
            raise ValueError("Runtime archive contents mismatch")
        for name in archive.namelist():
            if archive.read(name) != files[name]:
                raise ValueError("Runtime archive byte mismatch")
    manifest_data = (json.dumps(value, indent=2) + "\n").encode()
    manifest_path = output / "distribution.json"
    if manifest_path.exists() and manifest_path.read_bytes() != manifest_data:
        raise ValueError("Immutable distribution manifest collision")
    manifest_path.write_bytes(manifest_data)
    sums = {**files, "distribution.json": manifest_data}
    (output / "SHA256SUMS.txt").write_text("".join(f"{digest(data)}  {name}\n" for name, data in sorted(sums.items())), encoding="utf-8")
    print(f"DISTRIBUTION {version} build={build_id} source_dirty={dirty} assets={len(files)}")
    return value

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--dirty", action="store_true")
    args = parser.parse_args()
    build(args.bin, args.output, args.commit, args.dirty)
