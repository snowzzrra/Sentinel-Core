"""Reopen final runtime assets and check external checksums without loading a game."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path


def verify(directory):
    manifest = json.loads((directory / "distribution.json").read_text())
    records = {entry["path"]: entry for entry in manifest["artifacts"]}
    if len(records) != len(manifest["artifacts"]):
        raise ValueError("Duplicate artifact records")
    for name, record in records.items():
        data = (directory / name).read_bytes()
        if len(data) != record["size"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ValueError(f"Artifact bytes disagree: {name}")
    with zipfile.ZipFile(directory / "sentinel-runtime.zip") as archive:
        expected = set(records) - {"sentinel-runtime.zip"}
        if len(archive.namelist()) != len(expected) or set(archive.namelist()) != expected:
            raise ValueError("Incomplete or duplicate runtime ZIP assets")
        for name in expected:
            if archive.read(name) != (directory / name).read_bytes():
                raise ValueError(f"Direct/ZIP bytes disagree: {name}")
    expected = set(records) | {"distribution.json"}
    checked = set()
    for line in (directory / "SHA256SUMS.txt").read_text().splitlines():
        digest, name = line.split("  ", 1)
        if name not in expected or name in checked or hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
            raise ValueError("Invalid external checksum set")
        checked.add(name)
    if checked != expected:
        raise ValueError("Incomplete external checksums")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    print(json.dumps(verify(parser.parse_args().directory), indent=2))
