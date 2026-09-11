"""Private START/CAPTURE/FINISH driver for one explicitly selected A retest.

Only START, explicitly invoked by the maintainer, launches Steam. No stage
launches DOOM, installs DLLs, submits saves or restores original data.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
import uuid
import zipfile

import prepare_vanilla_backup as protection


REQUIRED = ("sentinel_core.dll", "msimg32.dll", "sentinel_probe.exe", "prepare_vanilla_backup.py",
            "compare_vanilla_campaign.py", "Prepare-MilestoneA.ps1", "Retest-MilestoneA.ps1", "retest_milestone_a.py")
QUERIES = ("basic", "engine", "native", "save_admission", "save_installation", "save_context", "save_write")
MAX_OUTPUT = 256 * 1024
COMMON = "result operation wire_version core_abi core_version build_id target_pid server_pid process_created instance_id failure_stage win32_error target_state target_wait_error verified_process_created".split()
ALLOWED = {
    "basic": "host_kind core_state ipc_state ipc_error core_capabilities inspection_capabilities initialization_count last_result engine_integration gameplay_safety game_build",
    "engine": "engine_abi engine_query_capability sequence sampled_at_ms sample_age_ms sample_duration_ms sample_reason pe_reason pe_machine pe_timestamp pe_image_size pe_entry_rva disk_sha256 disk_hash_reason profile locator_revision root_locator_reason root_signature_rva root_target_rva profile_runtime_validated gameplay_authorization",
    "native": "native_abi availability reason site_revision site_rva phase installed_hooks retained_module coverage_bits coverage_complete native_load_serial gameplay_authorization lifecycle_generation lifecycle depth checkpoint_flag_known checkpoint_flag event_sequence event_gap_count history_oldest history_overwritten callback_sequence callback_at_ms callback_thread_id native_owner_thread_id context_generation context_sampled_at_ms context_reason game_state queued claimed retained_results history_gap",
    "save_admission": "admission_abi state fault prepared_routes required_routes startup_qualified route_retained accepting_requests namespace_id native_root",
    "save_installation": "",
    "save_context": "save_abi save_query_capability sequence sampled_at_ms sample_age_ms sample_duration_ms sample_reason profile layout_revision root_locator_reason mutation_available mutation_reason provider native_runtime_evidence",
    "save_write": "write_abi operation_id sdk_sequence state directory flags file_count submitted completed pending_handles preparation_jobs preflight_jobs readback_verified persistence_verified reopen_verified",
}
FIELD_NAMES = ("root_available loading in_game map_present player_present cutscene_id root_present profile_manager_present "
               "provider_kind queued_requests pending_map_load save_job_witness request58_witness request50_witness "
               "selected_slot namespace_route native_operation_id native_completion").split()
INSTALLATION = "abi clock units attempt sequence phase last_completed_stage startup_observation validated created enabled cleanup_failures gaps".split()
EVENT = "stage target_group target_index target_name rva signature_offset sequence at_ms duration_ms result reason read_reason win32_error minhook_status byte_count byte_window_offset collision_rva expected_bytes actual_bytes".split()


class Refused(Exception):
    pass


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, data, create=False):
    path = Path(path)
    if create:
        with path.open("x", encoding="utf-8") as stream:
            json.dump(data, stream, indent=2)
            stream.write("\n")
        return
    pending = path.with_name(path.name + ".pending")
    with pending.open("x", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")
    os.replace(pending, path)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def load_config(path):
    config = read_json(path)
    paths = ("GameInstall", "SteamExe", "SteamAppRoot", "LocalProviderRoot", "OriginalBackupDirectory",
             "APRoot", "Python", "PowerShell7", "CandidateManifest", "EvidenceRoot", "ActiveRun")
    for name in paths:
        value = config.get(name)
        if not isinstance(value, str) or not Path(value).is_absolute():
            raise Refused("missing_or_nonabsolute_configuration_" + name)
    roots = config.get("UninstallRoots")
    if not isinstance(roots, list) or not roots or any(not isinstance(p, str) or not Path(p).is_absolute() for p in roots):
        raise Refused("UninstallRoots_must_be_an_array_of_absolute_paths")
    if not re.fullmatch(r"[1-9][0-9]*", str(config.get("SteamAccount32", ""))):
        raise Refused("invalid_SteamAccount32")
    return config


def validate_candidate(config, installed=False):
    manifest_path = Path(config["CandidateManifest"])
    manifest = read_json(manifest_path)
    if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("build_id", "")):
        raise Refused("invalid_candidate_build_identity")
    if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("supported_game_sha256", "")):
        raise Refused("missing_supported_game_identity")
    entries = manifest.get("files", [])
    for name in REQUIRED:
        if sum(entry.get("name") == name for entry in entries) != 1:
            raise Refused("missing_or_duplicate_candidate_member_" + name)
    for entry in entries:
        name = entry.get("name", "")
        if Path(name).name != name or ":" in name or not re.fullmatch(r"[0-9a-f]{64}", entry.get("sha256", "")):
            raise Refused("invalid_candidate_file_entry")
        if sha(manifest_path.parent / name) != entry["sha256"]:
            raise Refused("candidate_hash_mismatch_" + name)
    hashes = {entry["name"]: entry["sha256"] for entry in entries}
    if sha(__file__) != hashes["retest_milestone_a.py"]:
        raise Refused("driver_is_not_the_selected_candidate")
    if installed:
        for name in ("sentinel_core.dll", "msimg32.dll"):
            if sha(Path(config["GameInstall"]) / name) != hashes[name]:
                raise Refused("installed_candidate_mismatch_" + name)
        if sha(Path(config["GameInstall"]) / "DOOMEternalx64vk.exe") != manifest["supported_game_sha256"]:
            raise Refused("installed_game_hash_mismatch")
    return manifest


def run_command(command, prefix, timeout, started=None):
    """Capture bounded stdout/stderr independently; only kill this helper on timeout."""
    prefix = Path(prefix)
    result = {"started_utc": utc(), "exit_code": None, "timed_out": False, "cancelled": False,
              "launch_error": None, "stdout_truncated": False, "stderr_truncated": False}
    write_json(str(prefix) + ".command.private.json", {"argv": command, "timeout_seconds": timeout}, create=True)
    outputs = {"stdout": bytearray(), "stderr": bytearray()}
    start = time.monotonic()
    try:
        environment = os.environ.copy()
        environment.pop("SENTINEL_AP_TEST_SESSION", None)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    except OSError:
        result["launch_error"] = "command_launch_failed"
    else:
        def read(channel):
            pipe = getattr(process, channel)
            try:
                while chunk := pipe.read(8192):
                    available = MAX_OUTPUT - len(outputs[channel])
                    outputs[channel].extend(chunk[:available])
                    if len(chunk) > available:
                        result[channel + "_truncated"] = True
            finally:
                pipe.close()
        threads = [threading.Thread(target=read, args=(channel,), daemon=True) for channel in outputs]
        for thread in threads:
            thread.start()
        if started:
            started(process)
        try:
            result["exit_code"] = process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
            result["timed_out"] = isinstance(error, subprocess.TimeoutExpired)
            result["cancelled"] = isinstance(error, KeyboardInterrupt)
            process.kill()
            process.wait()
        for thread in threads:
            thread.join(timeout=2)
        if any(thread.is_alive() for thread in threads):
            result["stdout_truncated"] = result["stderr_truncated"] = True
    result["finished_utc"] = utc()
    result["duration_ms"] = round((time.monotonic() - start) * 1000)
    for channel, data in outputs.items():
        with Path(str(prefix) + "." + channel + ".private.txt").open("xb") as stream:
            stream.write(data)
        result[channel + "_bytes_retained"] = len(data)
        result[channel] = data.decode("utf-8-sig", errors="replace")
    write_json(str(prefix) + ".result.private.json", {key: value for key, value in result.items() if key not in outputs}, create=True)
    return result


def debug_owner_present():
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenEventW.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR)
    kernel.OpenEventW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    for name in ("DBWIN_BUFFER_READY", "DBWIN_DATA_READY"):
        handle = kernel.OpenEventW(0x00100000, False, name)
        if handle:
            kernel.CloseHandle(handle)
            return True
        if ctypes.get_last_error() != 2:
            raise Refused("optional_capture_owner_cannot_be_established")
    return False


def debug_prerequisite(config):
    import winreg
    debug = config.get("DebugView", {})
    if not debug.get("Requested"):
        return "optional_capture_not_requested"
    if not Path(debug.get("Path", "")).is_file() or sha(debug["Path"]) != debug.get("Sha256"):
        return "optional_capture_tool_hash_unavailable"
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Sysinternals\DebugView", access=winreg.KEY_READ) as key:
            accepted, kind = winreg.QueryValueEx(key, "EulaAccepted")
        if accepted != 1 or kind != winreg.REG_DWORD:
            return "optional_capture_EULA_not_already_accepted"
    except OSError:
        return "optional_capture_EULA_acceptance_not_established"
    return "optional_capture_owner_already_present" if debug_owner_present() else None


def debug_worker(config_path, directory):
    """Retain only our optional CLI child to record its eventual bounded outcome."""
    config = read_json(config_path)
    directory = Path(directory)
    ready = directory / "debug-ready.private.json"
    try:
        reason = debug_prerequisite(config)
        if reason:
            write_json(ready, {"state": "unavailable", "reason": reason}, create=True)
            return
        command = [config["DebugView"]["Path"], "--win32", "--no-kernel", "--no-global",
                   "--process-filter", "DOOMEternalx64vk.exe", "--filter", "*Sentinel Install*",
                   "--duration", "180", "--max-lines", "32", "--history", "32", "--log-limit", "1",
                   "--no-banner", "--log", str(directory / "debugview.private.log")]
        def ready_when_owned(process):
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and process.poll() is None:
                if debug_owner_present():
                    write_json(ready, {"state": "armed", "reason": "bounded_process_filtered_child_started",
                                      "capture_pid": process.pid, "at_utc": utc()}, create=True)
                    return
                time.sleep(0.05)
            write_json(ready, {"state": "unavailable", "reason": "optional_capture_did_not_arm"}, create=True)
        result = run_command(command, directory / "debugview", 190, started=ready_when_owned)
        if not ready.exists():
            write_json(ready, {"state": "unavailable", "reason": "optional_capture_launch_failed"}, create=True)
        write_json(directory / "debug-finished.private.json",
                   {key: value for key, value in result.items() if key not in ("stdout", "stderr")}, create=True)
    except (OSError, Refused, ValueError, KeyError, TypeError):
        if not ready.exists():
            write_json(ready, {"state": "unavailable", "reason": "optional_capture_prerequisite_unavailable"}, create=True)


def arm_debug(config, directory):
    reason = debug_prerequisite(config)
    if reason:
        return {"state": "unavailable", "reason": reason}
    worker = subprocess.Popen([config["Python"], "-B", __file__, "--debug-worker", str(directory / "config.json"), str(directory)],
                              creationflags=subprocess.CREATE_NO_WINDOW, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 5
    ready = directory / "debug-ready.private.json"
    while time.monotonic() < deadline:
        if ready.exists():
            return read_json(ready)
        if worker.poll() is not None:
            break
        time.sleep(0.05)
    return {"state": "unavailable", "reason": "optional_capture_worker_did_not_arm"}


PROCESS_SCRIPT = r"""
$ErrorActionPreference = 'Stop'
$items = @(Get-Process -Name 'DOOMEternalx64vk' -ErrorAction SilentlyContinue)
$records = @($items | ForEach-Object {
    $p = $_
    $modules = @($p.Modules | Where-Object { $_.ModuleName -in @('sentinel_core.dll','msimg32.dll') } | ForEach-Object {
        @{basename=$_.ModuleName; path=$_.FileName; sha256=(Get-FileHash -LiteralPath $_.FileName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    @{pid=$p.Id; path=$p.Path; process_created=$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString(); modules=$modules}
})
ConvertTo-Json -InputObject $records -Depth 5 -Compress
"""


def observe_game(config, prefix):
    result = run_command([config["PowerShell7"], "-NoProfile", "-NonInteractive", "-Command", PROCESS_SCRIPT], prefix, 8)
    if result["exit_code"] != 0 or result["stdout_truncated"]:
        raise Refused("game_process_inventory_unavailable")
    items = json.loads(result["stdout"])
    if not isinstance(items, list) or len(items) != 1:
        raise Refused("no_game_process" if items == [] else "ambiguous_game_processes")
    observed = items[0]
    expected = Path(config["GameInstall"]) / "DOOMEternalx64vk.exe"
    if Path(observed["path"]) != expected:
        raise Refused("game_executable_path_mismatch")
    return observed


def launch_steam(config, descriptor):
    environment = os.environ.copy()
    environment["SENTINEL_AP_TEST_SESSION"] = descriptor
    if config.get("DebugView", {}).get("Requested"):
        environment["SENTINEL_INSTALL_DEBUG"] = "1"
    # The environment is inherited only by this explicit Steam child.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([config["SteamExe"]], env=environment, startupinfo=startup,
                               cwd=str(Path(config["SteamExe"]).parent))
    return process.pid


def redact(text, config):
    text = str(text)[:4096]
    replacements = [(str(value), "<" + name.upper() + ">") for name, value in config.items() if isinstance(value, str)]
    replacements += [(str(value), "<UNINSTALL_ROOT>") for value in config["UninstallRoots"]]
    replacements += [(str(config["SteamAccount32"]), "<STEAM_ACCOUNT>")]
    for value, replacement in sorted(replacements, key=lambda item: len(item[0]), reverse=True):
        for spelling in {value, value.replace("\\", "/")}:
            text = re.sub(re.escape(spelling), lambda _: replacement, text, flags=re.IGNORECASE)
    text = re.sub(r"(?i)(?:[a-z]:[\\/]|\\\\)[^\r\n\t\"<>]*", "<PRIVATE_PATH>", text)
    text = re.sub(r"(?i)\b(password|token|secret|authorization)\s*[:=]\s*\S+", r"\1=<REDACTED>", text)
    return text


def scalars(value, keys):
    if not isinstance(value, dict):
        return {}
    result = {}
    for key in keys:
        item = value.get(key)
        if key not in value:
            continue
        if item is None or isinstance(item, (bool, int)):
            result[key] = item
        elif isinstance(item, str):
            # Protocol scalars contain no paths or unrestricted text.
            limit = 64 if key in ("expected_bytes", "actual_bytes") else 128
            result[key] = item if len(item) <= limit and re.fullmatch(r"[A-Za-z0-9_. /:+-]*", item) and not re.search(r"[A-Za-z]:[/\\]", item) else "redacted_nonprotocol_value"
    return result


def safe_response(query, value):
    result = scalars(value, COMMON + ALLOWED.get(query, "").split())
    if query in ("engine", "save_context") and isinstance(value.get("fields"), dict):
        result["fields"] = {key: scalars(value["fields"][key], ("validity", "reason", "value", "win32_error"))
                            for key in FIELD_NAMES if key in value["fields"]}
    if query == "native":
        result["validator_reasons"] = [s for s in value.get("validator_reasons", [])[:3] if isinstance(s, str) and re.fullmatch(r"[a-z0-9_]+", s)]
        result["events"] = [scalars(event, "sequence generation at_ms kind lifecycle thread_id depth".split())
                            for event in value.get("events", [])[:64] if isinstance(event, dict)]
    if query == "save_write" and isinstance(value.get("native_result"), dict):
        result["native_result"] = scalars(value["native_result"], ("state", "outcome", "value"))
    if query == "save_installation" and isinstance(value.get("installation"), dict):
        detail = value["installation"]
        result["installation"] = scalars(detail, INSTALLATION)
        for name in ("active", "primary_failure", "cleanup_failure"):
            if name in detail:
                result["installation"][name] = scalars(detail[name], EVENT) if isinstance(detail[name], dict) else None
    return result


class Run:
    def __init__(self, config_path, stage):
        self.config_path = Path(config_path)
        current = load_config(self.config_path)
        self.reference = Path(current["ActiveRun"])
        if stage == "START":
            completed_reference = False
            if self.reference.exists():
                previous = read_json(self.reference)
                previous_directory = Path(previous["run_directory"])
                if (previous_directory.parent != Path(current["EvidenceRoot"]) or previous_directory.name != previous["run_id"]
                    or not read_json(previous_directory / "private/state.json").get("finished")
                    or not (previous_directory / (previous["run_id"] + "-shareable.zip")).is_file()):
                    raise Refused("active_run_already_exists_use_CAPTURE_or_FINISH")
                completed_reference = True
            self.config = current
            self.directory = Path(current["EvidenceRoot"]) / ("retest-" + uuid.uuid4().hex)
            self.directory.mkdir(parents=True)
            (self.directory / "private").mkdir()
            self.state = {"schema": "sentinel-a-retest-private-v1", "run_id": self.directory.name,
                          "started_utc": utc(), "finished": False, "stages": [], "commands": [],
                          "captures": 0, "process": None, "descriptor": None,
                          "debug_capture": {"state": "unavailable", "reason": "optional_process_filtered_capture_not_configured"}}
            write_json(self.directory / "private/config.json", current, create=True)
            self.reference.parent.mkdir(parents=True, exist_ok=True)
            write_json(self.reference, {"run_directory": str(self.directory), "run_id": self.directory.name}, create=not completed_reference)
            self.save()
        else:
            reference = read_json(self.reference)
            self.directory = Path(reference["run_directory"])
            if self.directory.parent != Path(current["EvidenceRoot"]) or self.directory.name != reference["run_id"]:
                raise Refused("active_run_reference_mismatch")
            self.config = read_json(self.directory / "private/config.json")
            self.state = read_json(self.directory / "private/state.json")
            if self.state["run_id"] != reference["run_id"] or self.state["finished"]:
                raise Refused("run_mismatched_or_already_finished")
            if stage == "CAPTURE" and current != self.config:
                raise Refused("configuration_changed_since_START")

    def save(self):
        write_json(self.directory / "private/state.json", self.state)

    def prefix(self, label):
        return self.directory / "private" / (f"{len(self.state['commands']) + 1:03d}-" + label)

    def execute(self, label, command, timeout=8):
        prefix = self.prefix(label)
        raw = run_command(command, prefix, timeout)
        record = {"query": label, **{key: value for key, value in raw.items() if key not in ("stdout", "stderr")}}
        try:
            response = json.loads(raw["stdout"])
        except (json.JSONDecodeError, TypeError):
            response = None
        record["json_state"] = "parsed" if isinstance(response, dict) else "missing_or_partial"
        self.state["commands"].append(record)
        self.save()
        if record["cancelled"]:
            raise KeyboardInterrupt
        return record, raw, response

    def start(self):
        config = self.config
        print("Resolved private configuration (kept local):\n" + json.dumps(config, indent=2))
        for name in ("Python", "PowerShell7", "SteamExe"):
            if not Path(config[name]).is_file():
                raise Refused("configured_tool_missing_" + name)
        manifest = validate_candidate(config, installed=True)
        self.state["manifest"] = manifest
        self.state["manifest_sha256"] = sha(config["CandidateManifest"])
        self.save()
        protection.require_stopped()
        params = {"Python": config["Python"], "SteamAccount": str(config["SteamAccount32"]),
                  "SteamAppRoot": config["SteamAppRoot"], "LocalProviderRoot": config["LocalProviderRoot"],
                  "BackupDirectory": config["OriginalBackupDirectory"], "APRoot": config["APRoot"],
                  "UninstallRoot": config["UninstallRoots"]}
        params_path = self.directory / "private/preparation-parameters.json"
        write_json(params_path, params, create=True)
        wrapper = Path(config["CandidateManifest"]).parent / "Prepare-MilestoneA.ps1"
        # JSON carries a real string[]; no user-supplied PowerShell expressions.
        def literal(value):
            return "'" + str(value).replace("'", "''") + "'"
        script = "$ErrorActionPreference='Stop'; $p=Get-Content -LiteralPath " + literal(params_path) + " -Raw | ConvertFrom-Json -AsHashtable; & " + literal(wrapper) + " @p"
        record, raw, _ = self.execute("protect_and_prepare", [config["PowerShell7"], "-NoProfile", "-NonInteractive", "-Command", script], 180)
        if record["exit_code"] != 0 or record["stdout_truncated"]:
            raise Refused("original_protection_or_preparation_refused")
        # The existing wrapper emits the protection JSON, then its own receipt.
        decoder = json.JSONDecoder()
        text = raw["stdout"].strip()
        receipts = []
        while text:
            value, end = decoder.raw_decode(text)
            receipts.append(value)
            text = text[end:].lstrip()
        receipt = receipts[-1] if receipts else {}
        if receipt.get("result") != "guarded_prelaunch_prepared" or receipt.get("build_id") != manifest["build_id"]:
            raise Refused("invalid_preparation_receipt")
        descriptor = Path(receipt["descriptor"])
        if descriptor.parent != Path(config["APRoot"]) or not descriptor.is_file():
            raise Refused("prepared_descriptor_path_mismatch")
        self.state["descriptor"] = str(descriptor)
        self.state["namespace_id"] = receipt["namespace_id"]
        record["json_state"] = "verified_preparation_receipt"
        record["response"] = {"result": receipt["result"], "build_id": receipt["build_id"], "namespace_id": receipt["namespace_id"]}
        self.save()
        protection.require_stopped()
        try:
            self.state["debug_capture"] = arm_debug(config, self.directory / "private")
        except (OSError, Refused, ValueError):
            self.state["debug_capture"] = {"state": "unavailable", "reason": "optional_capture_prerequisite_unavailable"}
        self.save()
        self.state["steam_launch_pid"] = launch_steam(config, str(descriptor))
        print("Steam started with this run's AP descriptor. Open DOOM manually and remain in the main menu; do not enter a campaign.")

    def capture(self):
        if not self.state["descriptor"]:
            raise Refused("START_did_not_prepare_an_identity")
        manifest = validate_candidate(self.config)
        if manifest["build_id"] != self.state["manifest"]["build_id"] or sha(self.config["CandidateManifest"]) != self.state["manifest_sha256"]:
            raise Refused("candidate_changed_since_START")
        self.state["captures"] += 1
        capture_id = self.state["captures"]
        namespace_refusal = None
        probe = Path(self.config["CandidateManifest"]).parent / "sentinel_probe.exe"
        for index, query in enumerate(QUERIES):
            observed = observe_game(self.config, self.directory / "private" / f"capture-{capture_id}-{index}-process")
            previous = self.state["process"]
            if previous and any(str(previous[key]) != str(observed[key]) for key in ("pid", "process_created", "path")):
                raise Refused("game_process_replaced_remaining_queries_not_performed")
            if previous is None:
                self.state["process"] = observed
                self.state["process"]["instance_id"] = None
                self.save()
            arguments = [str(probe), "--pid", str(observed["pid"]), "--timeout-ms", "2000", "--json"]
            if query != "basic":
                arguments.append("--" + query.replace("_", "-"))
            record, raw, response = self.execute(query, arguments)
            if isinstance(response, dict):
                if response.get("result") == "ok" and not record["stdout_truncated"]:
                    expected = self.state["process"]
                    if (response.get("build_id") != manifest["build_id"] or
                        str(response.get("target_pid")) != str(expected["pid"]) or
                        str(response.get("server_pid")) != str(expected["pid"]) or
                        str(response.get("process_created")) != str(expected["process_created"]) or
                        not re.fullmatch(r"[0-9a-fA-F]{32}", str(response.get("instance_id", ""))) or
                        (query != "basic" and response.get("operation") != query)):
                        record["identity_state"] = "refused"
                        raise Refused("probe_process_or_candidate_identity_mismatch")
                    if expected["instance_id"] and expected["instance_id"] != response["instance_id"]:
                        record["identity_state"] = "replaced"
                        raise Refused("core_instance_replaced_remaining_queries_not_performed")
                    expected["instance_id"] = response["instance_id"]
                    record["identity_state"] = "verified"
                    if query == "save_admission":
                        namespace = self.state["namespace_id"]
                        actual_namespace, actual_root = response.get("namespace_id"), response.get("native_root")
                        if actual_namespace == namespace and actual_root == "ap-" + namespace[:40]:
                            record["namespace_state"] = "verified"
                        elif actual_namespace in (None, "") and actual_root in (None, ""):
                            record["namespace_state"] = "unavailable"
                        else:
                            record["namespace_state"] = "mismatch"
                        if record["namespace_state"] == "mismatch" or (response.get("state") == "admitted" and record["namespace_state"] != "verified"):
                            namespace_refusal = "AP_namespace_" + record["namespace_state"] + "_remaining_safe_queries_captured"
                record["response"] = safe_response(query, response)
            self.save()
            # Admission exit 8 is evidence, never a reason to skip safe queries.
        final = observe_game(self.config, self.directory / "private" / f"capture-{capture_id}-final-process")
        if any(str(final[key]) != str(self.state["process"][key]) for key in ("pid", "process_created", "path")):
            raise Refused("game_process_replaced_at_capture_end")
        hashes = {entry["name"]: entry["sha256"] for entry in manifest["files"]}
        modules = final.get("modules", [])
        if len(modules) != 2 or any(Path(module["path"]) != Path(self.config["GameInstall"]) / module["basename"].lower()
                                   or module["sha256"] != hashes.get(module["basename"].lower()) for module in modules):
            raise Refused("loaded_module_identity_mismatch_or_unavailable")
        if namespace_refusal:
            raise Refused(namespace_refusal)
        print("Read-only capture complete. Stay out of campaigns. Close DOOM and Steam normally before FINISH.")

    def finish(self, args):
        print("FINISH requires DOOM and Steam to have exited normally. Nothing will be force-stopped; unavailable comparison stays pending while evidence is exported.")
        self.state["operator"] = {"attribution": "operator_supplied_not_script_verified", "catalog": args.catalog,
                                  "selection": args.selection, "rollback": args.rollback,
                                  "note": redact(args.note, self.config)}
        comparison = {"state": "pending", "reason": "not_performed"}
        self.state["comparison"] = comparison
        if args.comparison_cancelled:
            comparison.update(state="cancelled", reason="operator_cancelled")
        else:
            try:
                protection.require_stopped()
                validate_candidate(self.config)
                if self.state.get("manifest_sha256") != sha(self.config["CandidateManifest"]):
                    raise Refused("candidate_changed_since_START")
                command = [self.config["Python"], "-B", str(Path(self.config["CandidateManifest"]).parent / "compare_vanilla_campaign.py")]
                for flag, key in (("steam-account", "SteamAccount32"), ("steam-app-root", "SteamAppRoot"),
                                  ("local-provider-root", "LocalProviderRoot"), ("backup-directory", "OriginalBackupDirectory")):
                    command.extend(["--" + flag, str(self.config[key])])
                record, _, response = self.execute("campaign_comparison", command, 180)
                comparison.update(state="completed" if record["exit_code"] == 0 else "failed", reason="comparator_result")
                if isinstance(response, dict):
                    comparison["response"] = scalars(response, "result baseline_campaign_files current_campaign_files added removed modified selection_verification".split())
            except KeyboardInterrupt:
                comparison.update(state="cancelled", reason="operator_cancelled_during_comparison")
            except (protection.Refused, Refused, OSError, ValueError):
                comparison.update(state="pending", reason="stopped_check_or_comparison_prerequisite_unavailable")
        self.state["finished"] = True
        self.state["finished_utc"] = utc()
        print("Rollback, only after DOOM/Steam are stopped: restore the retained matched DLL pair, or remove only the test sentinel_core.dll/msimg32.dll according to the candidate's manual rollback instructions. Never restore saves automatically.")

    def export(self):
        self.state["debug_capture"].update(capture_finished=False, exit_code=None)
        debug_finished = self.directory / "private/debug-finished.private.json"
        if debug_finished.exists():
            finished = read_json(debug_finished)
            self.state["debug_capture"].update({"exit_code": finished["exit_code"], "timed_out": finished["timed_out"],
                                              "cancelled": finished["cancelled"], "capture_finished": True})
        self.state["debug_capture"]["private_log_present"] = (self.directory / "private/debugview.private.log").is_file()
        manifest = self.state.get("manifest", {})
        process = self.state.get("process")
        hashes = {entry["name"]: entry["sha256"] for entry in manifest.get("files", [])}
        modules = []
        for module in (process or {}).get("modules", []):
            name = module.get("basename", "").lower()
            if name in ("sentinel_core.dll", "msimg32.dll"):
                matches = (Path(module.get("path", "")) == Path(self.config["GameInstall"]) / name and module.get("sha256") == hashes.get(name))
                modules.append({"basename": name, "on_disk_sha256": module.get("sha256"), "matches_installed_candidate": matches})
        report = {"schema": "sentinel-milestone-a-retest-shareable-v1", "run_id": self.state["run_id"],
            "started_utc": self.state["started_utc"], "finished_utc": self.state.get("finished_utc"),
            "expected": {"build_id": manifest.get("build_id"), "product_version": manifest.get("product_version"),
                         "game_basename": "DOOMEternalx64vk.exe", "game_sha256": manifest.get("supported_game_sha256"),
                         "required_routes": manifest.get("required_routes")},
            "candidate": {"manifest_sha256": self.state.get("manifest_sha256"),
                          "files": [{"basename": name, "sha256": hashes[name]} for name in REQUIRED if name in hashes]},
            "prepared_namespace_id": self.state.get("namespace_id"),
            "process": {**scalars(process or {}, ("pid", "process_created", "instance_id")), "modules": modules},
            "stages": self.state["stages"], "commands": self.state["commands"],
            "comparison": self.state.get("comparison", {"state": "pending", "reason": "FINISH_not_run"}),
            "operator": self.state.get("operator", {"attribution": "not_supplied"}),
            "debug_capture": self.state["debug_capture"],
            "not_performed": ["native campaign creation/save/load/backup/reset/repair", "automatic visual catalog/selection verification",
                              "Steam Cloud compatibility validation", "runtime gameplay PASS"] +
                             ["read-only query: " + query for query in QUERIES
                              if not any(command["query"] == query for command in self.state["commands"])]}
        admissions = [item for item in report["commands"] if item["query"] == "save_admission"]
        admitted = bool(admissions and admissions[-1]["exit_code"] == 0 and admissions[-1].get("identity_state") == "verified"
                        and admissions[-1].get("namespace_state") == "verified"
                        and admissions[-1].get("response", {}).get("state") == "admitted")
        report["admission_observation"] = "admitted_in_read_only_snapshot" if admitted else "rejected_or_unavailable_no_gameplay"
        summary = ["Milestone A targeted retest", "", "Admission: " + report["admission_observation"],
                   "Expected routes: " + str(report["expected"]["required_routes"]), "Runtime PASS: not claimed.", ""]
        for item in admissions:
            response = item.get("response", {})
            summary.append(f"Admission exit={item['exit_code']}; state={response.get('state', 'unavailable')}; fault={response.get('fault', 'unavailable')}; routes={response.get('prepared_routes', 'unknown')}; namespace={item.get('namespace_state', 'unavailable')}.")
        for item in report["commands"]:
            failure = item.get("response", {}).get("installation", {}).get("primary_failure")
            if failure:
                summary.append("Installation primary failure: " + json.dumps(failure, sort_keys=True))
        summary += ["", "Campaign comparison: " + report["comparison"]["state"],
                    "Catalog/selection notes are operator supplied; the script did not verify UI state.",
                    "Optional debug capture: " + report["debug_capture"]["state"],
                    "Raw output/configuration remain private. Only SUMMARY.md and report.json are in this ZIP."]
        encoded = json.dumps(report, indent=2) + "\n"
        summary_text = "\n".join(summary) + "\n"
        share = self.directory / "shareable"
        share.mkdir()
        write_json(share / "report.json", report, create=True)
        (share / "SUMMARY.md").write_text(summary_text, encoding="utf-8")
        zip_path = self.directory / (self.state["run_id"] + "-shareable.zip")
        with zipfile.ZipFile(zip_path, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.writestr("SUMMARY.md", summary_text)
            bundle.writestr("report.json", encoded)
        print("Attach only this sanitized ZIP: " + str(zip_path))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("stage", choices=("START", "CAPTURE", "FINISH"))
    parser.add_argument("--catalog", choices=("not_observed", "unchanged", "changed"), default="not_observed")
    parser.add_argument("--selection", choices=("not_observed", "unchanged", "changed"), default="not_observed")
    parser.add_argument("--rollback", choices=("not_performed", "restored_pair", "removed_test_pair"), default="not_performed")
    parser.add_argument("--note", default="")
    parser.add_argument("--comparison-cancelled", action="store_true")
    args = parser.parse_args(argv)
    run = None
    try:
        config = load_config(args.config)
        lock_path = Path(config["ActiveRun"] + ".lock")
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        with lock_path.open("x"):
            pass
        try:
            run = Run(args.config, args.stage)
            stage = {"stage": args.stage, "started_utc": utc(), "state": "running", "failure": None}
            run.state["stages"].append(stage)
            try:
                getattr(run, args.stage.lower())(args) if args.stage == "FINISH" else getattr(run, args.stage.lower())()
                stage["state"] = "completed"
            except (Refused, protection.Refused, OSError, ValueError, KeyError, TypeError, KeyboardInterrupt) as error:
                stage["state"] = "cancelled" if isinstance(error, KeyboardInterrupt) else "failed"
                stage["failure"] = redact(str(error), run.config) if isinstance(error, (Refused, protection.Refused)) else "prerequisite_or_command_unavailable"
                print("Stage stopped: " + stage["failure"])
            stage["finished_utc"] = utc()
            run.save()
            if args.stage == "FINISH":
                run.export()
            print("Evidence directory: " + str(run.directory))
            return 0 if stage["state"] == "completed" else 1
        finally:
            lock_path.unlink()
    except (Refused, OSError, ValueError, KeyError, TypeError):
        print("Retest refused: configuration, exact run reference or stage lock is unavailable; no run was replaced.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--debug-worker":
        debug_worker(sys.argv[2], sys.argv[3])
    else:
        sys.exit(main())
