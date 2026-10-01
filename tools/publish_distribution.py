"""Publish a qualified, complete draft only through an explicit maintainer run."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from urllib.parse import quote

from verify_distribution import verify


def github(method, path, payload=None, *, binary=False, missing=False):
    command = ["gh", "api", "--method", method, path, "-H", "X-GitHub-Api-Version: 2022-11-28"]
    command += ["-H", "Accept: application/octet-stream" if binary else "Accept: application/vnd.github+json"]
    data = None
    if payload is not None:
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
        command += ["--input", "-", "-H", "Content-Type: application/octet-stream" if isinstance(payload, bytes) else "Content-Type: application/json"]
    result = subprocess.run(command, input=data, capture_output=True, check=False)
    if result.returncode:
        if missing and b"(HTTP 404)" in result.stderr:
            return None
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return result.stdout if binary else json.loads(result.stdout)


def publish(directory, repository, commit, version, mode, approve_stable=False, api=github):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Select an owner/repository")
    if not re.fullmatch(r"[a-f0-9]{40}", commit):
        raise ValueError("Select the full source commit")
    if mode not in {"draft", "prerelease", "stable"}:
        raise ValueError("Select an explicit publication mode")
    manifest = verify(directory)
    if manifest["product"] != "sentinel-core" or manifest["version"] != version or manifest["source_commit"] != commit or manifest["source_dirty"] is not False:
        raise ValueError("Release requires matching clean qualified source and version")
    if mode == "prerelease" and manifest["channel"] != "rc":
        raise ValueError("Prerelease requires an RC distribution")
    if mode == "stable" and (manifest["channel"] != "stable" or not approve_stable):
        raise ValueError("Stable requires a stable distribution and separate approval")

    prefix = f"repos/{repository}"
    if mode == "stable":
        environment = api("GET", f"{prefix}/environments/sentinel-stable")
        if not any(rule.get("type") == "required_reviewers" and rule.get("reviewers") for rule in environment["protection_rules"]):
            raise ValueError("Configure required reviewers on the sentinel-stable environment")

    paths = [entry["path"] for entry in manifest["artifacts"]] + ["distribution.json", "SHA256SUMS.txt"]
    assets = {Path(name).name: (directory / name).read_bytes() for name in paths}
    if len(assets) != len(paths):
        raise ValueError("Release asset basenames must be unique")
    tag = "v" + version
    def check_tag(ref):
        if not ref:
            return
        obj = ref["object"]
        seen = set()
        while obj["type"] == "tag":
            if obj["sha"] in seen:
                raise ValueError("Cyclic annotated tag")
            seen.add(obj["sha"])
            obj = api("GET", f"{prefix}/git/tags/{obj['sha']}")["object"]
        if obj["type"] != "commit" or obj["sha"] != commit:
            raise ValueError("Existing tag points to a different source commit")

    ref = api("GET", f"{prefix}/git/ref/tags/{quote(tag, safe='')}", missing=True)
    check_tag(ref)

    release = api("GET", f"{prefix}/releases/tags/{quote(tag, safe='')}", missing=True)
    if release and not ref and release["target_commitish"] != commit:
        raise ValueError("Existing draft targets a different source commit")
    if release:
        if release["prerelease"] != (manifest["channel"] == "rc") or not release["draft"] and mode != "draft" and release["prerelease"] != (mode == "prerelease"):
            raise ValueError("Existing published channel differs; promotion requires a new canonical version")
    if not release:
        run = os.environ.get("GITHUB_RUN_ID", "local")
        workflow = os.environ.get("GITHUB_WORKFLOW", "manual local invocation")
        body = f"Source: {commit}\nVersion: {version}\nBuild: {manifest['build_id']}\nWorkflow: {workflow}\nRun: https://github.com/{repository}/actions/runs/{run}\nToolchain: {json.dumps(manifest['toolchain'], sort_keys=True)}\n\n"
        body += "Assets (release name → package path; SHA256):\n" + "\n".join(f"{Path(name).name} → {name}: {hashlib.sha256((directory / name).read_bytes()).hexdigest()}" for name in paths)
        body += "\n\nSynthetic qualification only; gameplay evidence is recorded separately in distribution.json."
        release = api("POST", f"{prefix}/releases", {"tag_name": tag, "target_commitish": commit, "name": f"Sentinel Core {version}", "body": body, "draft": True, "prerelease": manifest["channel"] == "rc"})

    release_path = f"{prefix}/releases/{release['id']}"

    def check_assets(complete):
        remote = api("GET", f"{release_path}/assets?per_page=100")
        names = [entry["name"] for entry in remote]
        if len(names) != len(set(names)) or set(names) - assets.keys() or complete and set(names) != assets.keys():
            raise ValueError("Release has missing, duplicate or unexpected assets")
        for entry in remote:
            data = api("GET", f"{prefix}/releases/assets/{entry['id']}", binary=True)
            if entry["state"] != "uploaded" or data != assets[entry["name"]]:
                raise ValueError(f"Immutable release collision: {entry['name']}")
        return set(names)

    present = check_assets(complete=not release["draft"])
    for name in sorted(assets.keys() - present):
        api("POST", f"https://uploads.github.com/{release_path}/assets?name={quote(name, safe='')}", assets[name])
    check_assets(complete=True)
    if mode != "draft" and release["draft"]:
        release = api("PATCH", release_path, {"draft": False, "prerelease": mode == "prerelease", "make_latest": "false" if mode == "prerelease" else "true"})
        ref = api("GET", f"{prefix}/git/ref/tags/{quote(tag, safe='')}")
        check_tag(ref)
    print(f"RELEASE {tag} build={manifest['build_id']} assets={len(assets)} draft={release['draft']}")
    return release


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--mode", choices=("draft", "prerelease", "stable"), required=True)
    parser.add_argument("--approve-stable", action="store_true")
    args = parser.parse_args()
    publish(args.directory, args.repository, args.commit, args.version, args.mode, args.approve_stable)
