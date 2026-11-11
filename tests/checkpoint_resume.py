import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def signed(body):
    return body + b"sha256=" + hashlib.sha256(body).hexdigest().encode() + b"\n"


def main():
    host, temporary = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="resume-", dir=temporary) as directory:
        base = Path(directory)
        for candidate, matching in ((False, False), (True, False), (True, True)):
            root = base / f"candidate-{candidate}-{matching}"
            root.mkdir()

            def run(mode):
                result = subprocess.run([host, mode, root, "2", "native_read_c_auto"], capture_output=True, text=True, timeout=45)
                assert result.returncode == 0, result.stdout + result.stderr
                return result.stdout

            run("create")
            pending, = root.glob("*/checkpoint-A.pending")
            namespace = pending.parent
            original = pending.read_bytes()
            commit = (namespace / "checkpoint-A.commit").read_bytes()
            prefix = original.split(b"archive=", 1)[0].split(b"\n", 1)[1]
            prefix = prefix.replace(b"checkpoint=1\n", b"checkpoint=2\n").replace(b"side=A\n", b"side=B\n")
            transaction = prefix.split(b"transaction=", 1)[1].splitlines()[0]
            prefix = prefix.replace(transaction, b"c" * 32).replace(b"predecessor=" + b"0" * 64, b"predecessor=" + commit.split(b"sha256=")[-1].strip())
            started = signed(b"sentinel-checkpoint-started-v1\n" + prefix + b"state=native_save_pending\n")
            (namespace / "checkpoint-B.started").write_bytes(started)
            (namespace / "checkpoint-B.pending").write_bytes(started)
            old_archive = root / original.split(b"archive=", 1)[1].splitlines()[0].decode()
            manifest = (old_archive / "transport.manifest").read_bytes()
            native_directory = manifest.split(b"native_directory=", 1)[1].splitlines()[0].decode()
            native = root / "remote" / native_directory
            before = {p.name: p.read_bytes() for p in native.iterdir()}
            if candidate:
                checkpoint = bytes.fromhex(original.split(b"checkpoint_hex=", 1)[1].splitlines()[0].decode()).replace(b"checkpoint=1\n", b"checkpoint=2\n")
                archive = root / (old_archive.name[:-32] + "d" * 32)
                archive.mkdir()
                for payload in old_archive.glob("payload-*.bin"):
                    (archive / payload.name).write_bytes(payload.read_bytes())
                manifest = manifest.replace(manifest.split(b"checkpoint_hex=", 1)[1].splitlines()[0], checkpoint.hex().encode())
                (archive / "transport.manifest").write_bytes(manifest)
                staged = signed(b"sentinel-checkpoint-pending-v1\n" + prefix + b"archive=" + archive.name.encode() +
                                b"\nmanifest_sha256=" + hashlib.sha256(manifest).hexdigest().encode() + b"\ncheckpoint_hex=" + checkpoint.hex().encode() + b"\n")
                (namespace / "checkpoint-B.pending").write_bytes(staged)
                receipt = signed(b"sentinel-checkpoint-receipt-v1\npending_sha256=" + staged.split(b"sha256=")[-1].strip() +
                                 b"\nnative_success=1\ncallbacks=1\nreadback_terminal=1\ntransition=1\n")
                (namespace / "checkpoint-B.receipt").write_bytes(receipt)
            if not matching:
                (native / "game.details").write_bytes(b"interrupted native payload")
            output = run("resume")
            assert "automatic recovery " + ("interrupted_save_recovered" if matching else "previous_checkpoint_restored") in output, output
            assert "separate-process" in output, output
            generation = (3 if matching else 2)
            assert f"checkpoint={generation}" in output, output
            assert before["game.details"] in [p.read_bytes() for p in old_archive.glob("payload-*.bin")]
            assert list(namespace.glob("recovery.complete"))
            for record in root.glob("transport-backup-*/transport.manifest"):
                if b"role=quarantine\n" in record.read_bytes():
                    assert record.parent != old_archive
        print("PASS automatic rollback and receipt promotion followed by exact native parser/resume and a new verified save")


if __name__ == "__main__":
    main()
