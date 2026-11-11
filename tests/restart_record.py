from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    host, temporary = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="restart-", dir=temporary) as directory:
        for index, invalid in enumerate((b"stage=0\n", b"stage=1\n", b"stage=999999\n", b"corrupt")):
            root = Path(directory) / str(index)
            root.mkdir()

            def run(action, mode):
                result = subprocess.run([host, action, root, "3", mode], capture_output=True, text=True, timeout=60)
                assert result.returncode == 0, result.stdout + result.stderr

            run("create", "restart_pending")
            pending, = root.glob("*/stage.restart.pending")
            record = pending.read_bytes().split(b"\n", 2)[2]
            pending.write_bytes(record.replace(b"stage=7771000\n", invalid) if invalid != b"corrupt" else b"truncated")
            run("resume", "restart_normal")
            assert not pending.exists()
    print("PASS invalid persisted restart stages and truncated record")


if __name__ == "__main__":
    main()
