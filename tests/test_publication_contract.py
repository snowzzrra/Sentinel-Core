"""Offline publisher contract; API calls are simulated, with no credentials/network."""
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from publish_distribution import publish
import qualify_distribution

COMMIT = "a" * 40


def fixture(root, stable=False, dirty=False):
    files = {"sentinel_core.dll": b"core", "msimg32.dll": b"bootstrap", "sentinel_probe.exe": b"probe", "notices/LICENSE.txt": b"license"}
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    with zipfile.ZipFile(root / "sentinel-runtime.zip", "w") as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    files["sentinel-runtime.zip"] = (root / "sentinel-runtime.zip").read_bytes()
    manifest = {"product": "sentinel-core", "version": "1.0.0" if stable else "1.0.0-rc-1", "channel": "stable" if stable else "rc", "source_commit": COMMIT, "source_dirty": dirty, "build_id": "b" * 64, "toolchain": {"msvc": "14.44.35207"}, "artifacts": [{"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()]}
    data = json.dumps(manifest).encode()
    (root / "distribution.json").write_bytes(data)
    files["distribution.json"] = data
    (root / "SHA256SUMS.txt").write_text("".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in files.items()))
    return manifest["version"]


class GitHubFixture:
    def __init__(self):
        self.release = None
        self.assets = {}
        self.calls = []
        self.tag = None
        self.reviewers = []
        self.fail_upload_after = None

    def __call__(self, method, path, payload=None, *, binary=False, missing=False):
        self.calls.append((method, path))
        if "/environments/" in path:
            return {"protection_rules": [{"type": "required_reviewers", "reviewers": self.reviewers}]}
        if "/git/ref/" in path:
            return {"object": self.tag} if self.tag else None
        if "/git/tags/" in path:
            return {"object": {"type": "commit", "sha": COMMIT}}
        if method == "GET" and "/releases/tags/" in path:
            return dict(self.release) if self.release else None
        if method == "POST" and path.endswith("/releases"):
            self.release = {**payload, "id": 1}
            return dict(self.release)
        if method == "GET" and "/assets?" in path:
            return [{"id": name, "name": name, "state": "uploaded"} for name in self.assets]
        if method == "GET" and binary:
            return self.assets[path.rsplit("/", 1)[1]]
        if method == "POST" and "uploads.github.com" in path:
            if self.fail_upload_after is not None and len(self.assets) >= self.fail_upload_after:
                raise RuntimeError("Upload interrupted")
            name = parse_qs(urlsplit(path).query)["name"][0]
            assert name not in self.assets, "Cannot replace an existing asset"
            self.assets[name] = payload
            return {"id": name}
        if method == "PATCH":
            self.release.update(payload)
            self.tag = self.tag or {"type": "commit", "sha": COMMIT}
            return dict(self.release)
        raise AssertionError((method, path))


class PublicationContract(unittest.TestCase):
    def test_nested_core_qualification_reads_its_own_repository(self):
        args = type("Arguments", (), {"commit": COMMIT})()
        with patch.object(qualify_distribution, "run", side_effect=[COMMIT, " M tracked.py"]) as run:
            with self.assertRaisesRegex(ValueError, "Tracked CI source is dirty"):
                qualify_distribution.qualify(args)
        self.assertEqual(run.call_args_list[0].args, ("git", "-C", str(qualify_distribution.ROOT), "rev-parse", "HEAD"))
        self.assertEqual(run.call_args_list[1].args, ("git", "-C", str(qualify_distribution.ROOT), "status", "--porcelain", "--untracked-files=no"))

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version = fixture(self.root)
        self.api = GitHubFixture()

    def run_publish(self, mode="draft", approve=False):
        return publish(self.root, "owner/core", COMMIT, self.version, mode, approve, self.api)


    def test_interrupted_upload_stays_draft_and_resumes_without_replacement(self):
        self.api.fail_upload_after = 2
        with self.assertRaisesRegex(RuntimeError, "interrupted"):
            self.run_publish("prerelease")
        self.assertTrue(self.api.release["draft"])
        originals = dict(self.api.assets)
        self.api.fail_upload_after = None
        self.run_publish("prerelease")
        self.assertTrue(all(self.api.assets[name] == data for name, data in originals.items()))

    def test_collision_fails_without_mutation(self):
        self.run_publish()
        self.api.assets["sentinel_core.dll"] = b"different bytes"
        self.api.calls.clear()
        with self.assertRaisesRegex(ValueError, "Immutable release collision"):
            self.run_publish("prerelease")
        self.assertTrue(all(method == "GET" for method, _ in self.api.calls))
        self.assertTrue(self.api.release["draft"])




    def test_dirty_or_mismatched_source_refuses_before_api(self):
        fixture(self.root, dirty=True)
        with self.assertRaisesRegex(ValueError, "clean qualified source"):
            self.run_publish()
        fixture(self.root)
        with self.assertRaises(ValueError):
            publish(self.root, "owner/core", "c" * 40, self.version, "draft", api=self.api)
        self.assertEqual(self.api.calls, [])



if __name__ == "__main__":
    unittest.main()
