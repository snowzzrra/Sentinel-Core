import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    host, probe, temporary = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="checkpoint-probe-", dir=temporary) as directory:
        base = Path(directory)
        measurements = []
        for defect in ("archive_payload_failure", "archive_manifest_failure", "native_read_delta_backup"):
            root = base / defect
            root.mkdir()
            result = subprocess.run([host, "create", root, "1", defect], capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, result.stdout + result.stderr
            measurements.extend(line for line in result.stdout.splitlines() if line.startswith("archive files="))
            if defect == "native_read_delta_backup":
                result = subprocess.run([host, "resume", root, "1", defect], capture_output=True, text=True, timeout=60)
                assert result.returncode == 0, result.stdout + result.stderr
                measurements.extend(line for line in result.stdout.splitlines() if line.startswith("archive files="))
            descriptor = base / "descriptor.txt"
            descriptor.write_text(
                "sentinel-test-session-v3\nseed_hex=" + "synthetic-campaign-host".encode().hex() +
                "\nteam=0\nslot=1\ngeneration_fingerprint=" + "b" * 64 +
                "\nslot_data_revision=0.7.0\nsession_mod_contract_revision=2\nprovenance=synthetic-fixture\nroot=" +
                str(root) + "\ncampaign=unified\nstarting_stage=hub\ndifficulty=1\nintent=resume\n", encoding="utf-8", newline="\n")

            def plan():
                result = subprocess.run([probe, "--save-session-checkpoint-plan", descriptor], capture_output=True, text=True, timeout=15)
                assert result.returncode == 0, result.stdout + result.stderr
                return json.loads(result.stdout)

            record = plan()
            assert record["schema"] == 1 and record["format"] == "ab-v1", record
            expected = "save_interrupted" if defect.startswith("archive_") else "confirmed"
            assert record["checkpoint_state"] == expected, record
            assert record["resume_allowed"] == (expected == "confirmed"), record
            assert not record["native_admitted"] and not record["recovery_performed"], record
            checkpoint, = root.glob("*/campaign.checkpoint")
            assert checkpoint.read_bytes() == b"sentinel-checkpoint-ab-v1\n"
            assert bool(list(root.glob("*/checkpoint-*.commit"))) == (expected == "confirmed")
            assert record["preservation_required"] == (expected != "confirmed"), record
        print("PASS checkpoint probe and mandatory A/B archives")
        for line in measurements:
            print(line)


if __name__ == "__main__":
    main()
