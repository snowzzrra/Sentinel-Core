"""Persisted Milestone A RUN/PREPARE/EXPORT orchestration. Never launches the game."""
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
import contextlib
import ctypes
from ctypes import wintypes

import prepare_vanilla_backup as protection


REQUIRED = ("sentinel_core.dll", "msimg32.dll", "sentinel_probe.exe", "prepare_vanilla_backup.py",
            "compare_vanilla_campaign.py", "Prepare-MilestoneA.ps1", "Retest-MilestoneA.ps1", "retest_milestone_a.py")
QUERIES = ("basic", "engine", "native", "save_admission", "save_installation", "save_context", "save_write")
MAX_OUTPUT = 256 * 1024
MAX_STARTUP_LOG = 1024 * 1024  # 128 bounded native transition records, including PROFILE stages.
MAX_STARTUP_RECORD = 65536
B_STAGES = frozenset(('session profile_read profile_output profile_choice profile_capture profile_prepare '
    'profile_publish catalog creation difficulty transition checkpoint_factory provider sdk_prepare sdk_submit '
    'sdk_callback sdk_result readback_create readback_prepare readback_verify continuity resume parser startup_gate').split())
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


def startup_records(path, status):
    """Bound both input and individual allocations; discard overlong records."""
    remaining = MAX_STARTUP_LOG
    pending = bytearray()
    oversized = False
    with path.open('rb') as stream:
        while remaining:
            part = stream.readline(min(65536, remaining))
            if not part: break
            remaining -= len(part)
            if not oversized and len(pending) + len(part) <= 65536:
                pending.extend(part)
            else:
                oversized = True
                status['truncated'] = True
            if part.endswith(b'\n'):
                if not oversized:
                    try: value = json.loads(pending)
                    except (ValueError, UnicodeError): value = None
                    if isinstance(value, dict): yield value
                    else: status['malformed_records'] = status.get('malformed_records', 0) + 1
                pending.clear()
                oversized = False
        past_limit = remaining == 0 and bool(stream.read(1))
        if past_limit or oversized: status['truncated'] = True
        if pending and not oversized and not past_limit:
            try: value = json.loads(pending)
            except (ValueError, UnicodeError): status['truncated'] = True
            else:
                if isinstance(value, dict): yield value


def startup_latest(path):
    """The atomic final snapshot has its own bound, independent of history."""
    pending = bytearray()
    with path.open('rb') as stream:
        while len(pending) <= MAX_STARTUP_RECORD:
            part = stream.read(min(8192, MAX_STARTUP_RECORD + 1 - len(pending)))
            if not part: break
            pending.extend(part)
    if len(pending) > MAX_STARTUP_RECORD: raise ValueError('snapshot_oversized')
    value = json.loads(pending)
    if not isinstance(value, dict): raise ValueError('snapshot_not_object')
    return value


def automatic_rows(log):
    rows = list(log.get('records', []))
    latest = log.get('latest')
    if latest and (not rows or latest != rows[-1]): rows.append(latest)
    return rows


def safe_b_diagnostics(value):
    """Only literal event labels and bounded numeric facts cross the ZIP boundary."""
    if not isinstance(value, dict): return {}
    def number(value, signed=False):
        return type(value) is int and (-(1 << 63) if signed else 0) <= value < (1 << 63)
    def event(value):
        if not isinstance(value, dict) or value.get('stage') not in B_STAGES: return {}
        result = {key: value[key] for key in ('sequence', 'at_ms', 'operation', 'thread', 'status')
                  if number(value.get(key))}
        if not result.get('sequence') or result.get('status') not in range(1, 6): return {}
        result['stage'] = value['stage']
        predicate = value.get('predicate')
        result['predicate'] = predicate if isinstance(predicate, str) and re.fullmatch(r'[a-z][a-z0-9_]{0,95}', predicate) else 'redacted_nonprotocol_predicate'
        facts = value.get('facts', {})
        result['facts'] = {key: item for key, item in list(facts.items())[:16]
                           if isinstance(key, str) and re.fullmatch(r'[a-z][a-z0-9_]{0,47}', key) and
                           not re.search(r'(?:^|_)(?:address|pointer|ptr|handle|steamid|accountid|user_id)(?:_|$)', key) and
                           number(item, signed=True)} if isinstance(facts, dict) else {}
        return result
    result = {'sequence': value['sequence']} if number(value.get('sequence')) else {}
    result['first_failure'] = event(value.get('first_failure'))
    stages = value.get('stages', {})
    result['stages'] = {key: parsed for key in B_STAGES if isinstance(stages, dict)
                        and (parsed := event(stages.get(key))) and parsed['stage'] == key}
    return result


class CollectionUnavailable(Refused):
    def __init__(self, operation, result, observed_identity=None):
        super().__init__(operation + "_unavailable")
        self.operation, self.result = operation, result
        self.observed_identity = observed_identity


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
    pending = path.with_name(path.name + "." + uuid.uuid4().hex + ".pending")
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
    except OSError as error:
        result.update(launch_error="command_launch_failed", win32_error=getattr(error, "winerror", None), errno=error.errno)
        write_json(str(prefix) + ".launch-error.private.json", {"original_error": str(error)}, create=True)
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
    @{pid=$p.Id; path=$p.Path; process_created=$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
})
ConvertTo-Json -InputObject $records -Depth 5 -Compress
"""


def observe_game(config, prefix):
    result = run_command([config["PowerShell7"], "-NoProfile", "-NonInteractive", "-Command", PROCESS_SCRIPT], prefix, 8)
    if result["cancelled"]: raise KeyboardInterrupt
    if result["exit_code"] != 0 or result["stdout_truncated"]:
        raise CollectionUnavailable("process_identity", result)
    try: items = json.loads(result["stdout"])
    except (ValueError, TypeError): raise CollectionUnavailable("process_identity_JSON", result)
    if not isinstance(items, list) or len(items) != 1:
        raise Refused("no_game_process" if items == [] else "ambiguous_game_processes")
    observed = items[0]
    if not isinstance(observed, dict): raise CollectionUnavailable('process_identity_JSON', result)
    if not isinstance(observed.get('path'), str) or not observed['path']:
        raise CollectionUnavailable('process_path', result, observed)
    expected = Path(config["GameInstall"]) / "DOOMEternalx64vk.exe"
    if Path(observed["path"]) != expected:
        raise Refused("game_executable_path_mismatch")
    return observed


MODULE_SCRIPT = r"""
$ErrorActionPreference = 'Stop'
$operation = 'module_enumeration'
try {
    [Console]::Error.WriteLine($operation)
    $p = Get-Process -Id __PID__
    if ($p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne '__CREATED__') { throw 'process_identity_changed' }
    $modules = @($p.Modules | Where-Object { $_.ModuleName -in @('sentinel_core.dll','msimg32.dll') } | ForEach-Object {
        $operation = 'module_disk_hash'; [Console]::Error.WriteLine($operation)
        @{basename=$_.ModuleName; path=$_.FileName; sha256=(Get-FileHash -LiteralPath $_.FileName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $operation = 'system_forwarder_disk_hash'; [Console]::Error.WriteLine($operation)
    $system = Join-Path ([Environment]::SystemDirectory) 'msimg32.dll'
    @{modules=$modules; system_path=$system; system_sha256=(Get-FileHash -LiteralPath $system -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json -Depth 5 -Compress
} catch {
    [Console]::Error.WriteLine(($operation + ': ' + $_.Exception.ToString()))
    exit 1
}
"""


def observe_modules(config, process, prefix):
    script = MODULE_SCRIPT.replace('__PID__', str(int(process['pid']))).replace('__CREATED__', str(int(process['process_created'])))
    result = run_command([config['PowerShell7'], '-NoProfile', '-NonInteractive', '-Command', script], prefix, 8)
    if result['cancelled']: raise KeyboardInterrupt
    if result['exit_code'] != 0 or result['stdout_truncated']:
        raise CollectionUnavailable('module_collection', result)
    try:
        value = json.loads(result['stdout'])
        if not isinstance(value, dict) or not isinstance(value.get('modules'), list): raise ValueError()
        return value
    except (ValueError, TypeError): raise CollectionUnavailable('module_collection_JSON', result)


def module_roles(config, manifest, observation):
    api = protection.kernel()
    api.GetSystemDirectoryW.argtypes = [wintypes.LPWSTR, wintypes.UINT]
    buffer = ctypes.create_unicode_buffer(32768)
    length = api.GetSystemDirectoryW(buffer, len(buffer))
    if not length or length >= len(buffer): raise Refused('system_directory_identity_unavailable')
    system = Path(buffer.value) / 'msimg32.dll'
    hashes = {item['name']: item['sha256'] for item in manifest['files']}
    expected = {Path(config['GameInstall']) / name: ('game_core' if name == 'sentinel_core.dll' else 'game_proxy', hashes[name])
                for name in ('sentinel_core.dll', 'msimg32.dll')}
    if Path(observation.get('system_path', '')) != system or not re.fullmatch('[0-9a-f]{64}', observation.get('system_sha256', '')):
        raise Refused('system_forwarder_reference_identity_mismatch')
    expected[system] = ('windows_system_forwarder', observation['system_sha256'])
    rows = []; seen = set()
    for module in observation['modules']:
        name = module.get('basename', '').lower()
        role, digest = expected.get(Path(module.get('path', '')), ('untrusted_same_name', None))
        valid = role != 'untrusted_same_name' and module.get('sha256') == digest and name == Path(module['path']).name.lower()
        verification = 'verified_path_and_disk_hash' if valid else 'path_or_disk_hash_mismatch'
        if role in seen and role != 'untrusted_same_name': verification = 'duplicate_role'
        seen.add(role)
        rows.append({**module, 'role': role, 'verification': verification})
    mismatch = any(row['verification'] != 'verified_path_and_disk_hash' for row in rows)
    state = 'verified_mismatch' if mismatch else 'verified' if {'game_core', 'game_proxy'} <= seen else 'not_yet_observable'
    return state, rows


class Handoff:
    """One-use live owner lease. The local descriptor alone cannot activate AP."""
    def __init__(self, run):
        self.path = Path(run.config["GameInstall"]) / "sentinel-prelaunch.txt"
        self.api = protection.kernel()
        self.api.CreateSemaphoreW.argtypes = [ctypes.c_void_p, wintypes.LONG, wintypes.LONG, wintypes.LPCWSTR]
        self.api.CreateSemaphoreW.restype = wintypes.HANDLE
        self.handle = None
        self.published = False
        run_id = run.state["run_id"].removeprefix("retest-")
        reference = run.state["protection"]["reference_manifest_sha256"]
        created = process_created()
        self.text = (f"sentinel-run-v1\nrun={run_id}\nprotection={reference}\nowner={os.getpid()}\ncreated={created}\n" +
                     Path(run.state["descriptor"]).read_text(encoding="utf-8"))
        if os.environ.get("SENTINEL_AP_TEST_SESSION"):
            raise Refused("prelaunch_conflicting_environment_descriptor")
        lease_key = hashlib.sha256(self.text.encode("utf-8")).hexdigest()
        run.state["control_sha256"] = lease_key
        self.handle = self.api.CreateSemaphoreW(None, 1, 1, "Local\\SentinelA-" + lease_key)
        if not self.handle or ctypes.get_last_error() == 183:
            self.close()
            raise Refused("prelaunch_live_lease_creation_failed")
        try:
            phase = run.state.get('campaign_case', {}).get('phase')
            record_name = 'handoff-' + phase + '.private.json' if phase else 'handoff.private.json'
            write_json(run.directory / 'private' / record_name, {"text": self.text, "owner": os.getpid(), "created": created}, create=True)
            run.state['handoff_record'] = record_name
            with self.path.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(self.text)
            self.published = True
            run.state["handoff_armed"] = True
            run.save()
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None

    def retire(self):
        self.close()
        # Only this run's ephemeral publication; its exact bytes remain private.
        if self.published and self.path.exists():
            if self.path.read_text(encoding="utf-8") != self.text:
                raise Refused("prelaunch_publication_changed_cleanup_refused")
            self.path.unlink()


def process_created():
    api = protection.kernel()
    api.GetCurrentProcess.restype = wintypes.HANDLE
    values = [wintypes.FILETIME() for _ in range(4)]
    api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    if not api.GetProcessTimes(api.GetCurrentProcess(), *(ctypes.byref(v) for v in values)):
        raise ctypes.WinError(ctypes.get_last_error())
    return (values[0].dwHighDateTime << 32) | values[0].dwLowDateTime


def safe_failure(error, config, stage):
    value = protection.failure_record(error, stage)
    value["operation"] = stage
    if isinstance(error, Refused):
        value.update(reason=redact(str(error), config), distinction=redact(str(error), config), stage=stage)
    return value


def redact(text, config):
    text = str(text)[:4096]
    replacements = [(str(value), "<" + name.upper() + ">") for name, value in config.items() if isinstance(value, str) and name != 'Scenario']
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


def safe_startup_record(item):
    trace = item.get("profile", {})
    step_keys = ("first_ms", "changed_ms", "elapsed_ms", "status", "predicate", "native_attempted", "native_state", "native_outcome", "native_value",
                 "read_reason", "read_error", "read_requested", "read_offset", "read_size")
    profile = {**scalars(trace, ("request_id", "identity_kind", "identity_matched", "deadline_basis", "account_network_state", "downstream_refusals", "first_failed_stage")),
        "ownership": scalars(trace.get("ownership", {}), ("lifetime", "baseline_ready", "session_live", "stable_owner", "same_profile", "same_manager", "same_shell", "same_user")),
        "first_failure": scalars(trace.get("first_failure", {}), step_keys),
        "steps": {key: scalars(trace.get("steps", {}).get(key, {}), step_keys) for key in
            ("request", "profile_created", "catalog_created", "first_poll", "prepare", "decode", "transport", "catalog_poll", "catalog", "reader", "framing", "checksum", "parse", "overlay", "application", "root", "admission", "write_after_refusal", "output_validation")}}
    row = {**scalars(item, ("schema", "control_sha256", "pid", "process_created", "build_id", "at_ms", "engine_reason")), "profile": profile,
        "startup_route": scalars(item.get("startup_route", {}), ("at_ms", "adapter", "operation", "session_state", "native_phase", "startup_entered", "root_qualified", "manager_available", "provider_available", "native_user_available", "delegated_account_query", "caller_class", "caller_rva", "source_kind", "source_length", "source_step", "source_read_reason", "source_read_error", "stack_count", *(f"stack_rva_{i}" for i in range(8)))),
        'campaign': scalars(item.get('campaign', {}), ('enabled', 'resumed', 'phase', 'reason', 'slot', 'map', 'difficulty',
            'effective_difficulty', 'loaded_difficulty', 'changes_blocked', 'source_verified', 'parser_completed', 'parser_result', 'native_saved',
            'readback_verified', 'continuity_persisted', 'native_factory_matched', 'operation', 'checkpoint', 'source_checkpoint', 'generation_before', 'generation_after', 'map_active',
            'save_ready', 'failure_at_ms', 'transition_event', 'transition_at_ms', 'native_return', 'transition_depth', 'transition_observation_reason',
            'transition_observed', 'transition_ended', 'transition_abnormal', 'transition_state', 'transition_state_read', 'transition_map_read', 'transition_difficulty_read',
            'transition_map', 'checkpoint_at_ms', 'checkpoint_generation', 'checkpoint_depth', 'checkpoint_state', 'checkpoint_difficulty',
            'checkpoint_state_read', 'checkpoint_map_read', 'checkpoint_difficulty_read')),
        "admission": scalars(item.get("admission", {}), ("state", "fault", "flags", "prepared_routes", "required_routes", "namespace_id")),
        "installation": {**scalars(item.get("installation", {}), ("phase", "sequence", "startup_observation", "last_completed_stage", "validated", "created", "enabled")),
            **{key: scalars(item.get("installation", {}).get(key) or {},
              ("sequence", "stage", "reason", "at_ms", "duration_ms", "target_group", "target_index", "rva", "result", "win32_error", "minhook_status", "read_reason", "expected_bytes", "actual_bytes"))
               for key in ("primary_failure", "cleanup_failure", "active")}}}
    row['campaign']['parser_trace'] = scalars(item.get('campaign', {}).get('parser_trace', {}),
        ('at_ms', 'source', 'disposition', 'session_state', 'directory_read', 'prefix_read', 'native_completion', 'exact_resume'))
    row["b_diagnostics"] = safe_b_diagnostics(item.get("b_diagnostics"))
    storage = item.get('diagnostic_storage', {})
    row['diagnostic_storage'] = {key: storage[key] for key in ('history_records', 'history_bytes', 'history_error', 'latest_error')
        if isinstance(storage, dict) and type(storage.get(key)) is int and 0 <= storage[key] < (1 << 63)}
    return row




class Run:
    @staticmethod
    def campaign_failure(records, options):
        failures = []
        for row in records:
            fact, trace = row.get('campaign', {}), row.get('profile', {})
            diagnostic = row.get('b_diagnostics', {}).get('first_failure', {})
            if diagnostic.get('status') == 4 and diagnostic.get('sequence'):
                failures.append({'fault': 'native_b', 'reason': diagnostic['predicate'],
                    'at_ms': diagnostic.get('at_ms') or row.get('at_ms'), 'timing': 'native_b_event',
                    'first_failed_stage': diagnostic['stage'], 'first_failure': diagnostic, 'campaign': fact})
            startup = row.get('installation', {}).get('primary_failure', {})
            if startup.get('stage') in (19, 'startup') and row.get('admission', {}).get('fault') in (6, 'missed_startup'):
                failures.append({'fault': 'native_startup', 'reason': 'missed_startup',
                    'at_ms': startup.get('at_ms') or row.get('at_ms'), 'timing': 'native_startup_event',
                    'first_failed_stage': 'startup', 'first_failure': startup, 'campaign': fact,
                    'route': row.get('startup_route', {'adapter': 'not_retained_in_this_build'})})
            if fact.get('enabled') and (fact.get('reason') not in (None, 'none') or fact.get('difficulty') != options['difficulty']):
                failures.append({'fault': 'native_campaign', 'reason': fact.get('reason') if fact.get('reason') != 'none' else 'options_mismatch',
                    'at_ms': fact.get('failure_at_ms') or row.get('at_ms'),
                    'timing': 'native_event' if fact.get('failure_at_ms') else 'first_failed_sample', 'campaign': fact})
            refusal = trace.get('first_failure', {})
            if row.get('admission', {}).get('fault') == 15 and trace.get('first_failed_stage') not in (None, 'none', 'write_after_refusal'):
                failures.append({'fault': 'native_profile', 'reason': refusal.get('predicate', 'profile_refused'),
                    'at_ms': refusal.get('changed_ms') or row.get('at_ms'), 'timing': 'native_profile_refusal',
                    'first_failed_stage': trace['first_failed_stage'], 'first_failure': refusal, 'campaign': fact})
        if not failures: return None
        result = min(failures, key=lambda item: (item.get('at_ms') or float('inf'), item.get('fault') != 'native_b'))
        return {**result, 'clock': 'GetTickCount64_ms_exact_process'}

    @staticmethod
    def campaign_lifecycle(state, directory, require_export=True):
        case = state.get('campaign_case')
        if not case: return 'no_campaign', None
        if case['phase'] == 'completed': return 'completed_B', None
        if case['phase'] in ('prepare_resume', 'resume'): return 'completed_create_awaiting_resume', None
        records = automatic_rows(state.get('automatic_log', {}))
        process = state.get('process') or {}
        failure = Run.campaign_failure(records, case['options'])
        comparison = state.get('comparison', {})
        response = comparison.get('response', {})
        facts = [row['campaign'] for row in records if row.get('campaign', {}).get('enabled')]
        exact = bool(process.get('instance_id') and records and all(
            str(row.get('pid')) == str(process.get('pid')) and str(row.get('process_created')) == str(process.get('process_created')) and
            row.get('build_id') == state.get('manifest', {}).get('build_id') and
            (row.get('admission', {}).get('namespace_id') == state.get('namespace_id') or
             (row.get('admission', {}).get('state') == 0 and row.get('campaign', {}).get('enabled') is False)) for row in records))
        # Both terminal errors occur only after observe_launch saw normal exit,
        # require_stopped and the exact comparison. This also recognizes old
        # evidence without rewriting its create/pending state bytes.
        closed = any(stage.get('failure') in ('campaign_create_proof_incomplete_do_not_recreate',
            'campaign_failed_preserved_do_not_recreate') for stage in state.get('stages', []))
        before_checkpoint = bool(facts and all(fact.get('checkpoint') == 0 and fact.get('operation') == 0 and
            all(fact.get(key) is False for key in ('native_saved', 'readback_verified', 'continuity_persisted', 'native_factory_matched')) for fact in facts))
        faulted = any(row.get('admission', {}).get('state') in (4, 5) and row.get('admission', {}).get('fault') for row in records)
        if not (case['phase'] == 'create' and failure and faulted and state.get('finished') and closed and exact and before_checkpoint and
            not state.get('automatic_log', {}).get('truncated') and
            state.get('capture_health', {}).get('module_state') == 'verified' and comparison.get('state') == 'completed' and
            response.get('result') == 'vanilla_campaign_unchanged' and all(response.get(key) == 0 for key in ('added', 'removed', 'modified'))):
            return 'ambiguous_or_interrupted_create', None
        reference = state['protection']
        if sha(Path(reference['reference_directory']) / protection.MANIFEST) != reference['reference_manifest_sha256']:
            raise Refused('terminal_case_reference_changed')
        if not require_export: return 'terminal_failed_before_checkpoint', None
        archive = directory / (state['run_id'] + '-shareable.zip')
        if not archive.is_file(): return 'terminal_failure_awaiting_export', None
        with zipfile.ZipFile(archive) as bundle:
            report = json.loads(bundle.read('report.json'))
            if set(bundle.namelist()) != {'report.json', 'SUMMARY.md'} or report.get('run_id') != state['run_id'] or report.get('comparison') != comparison:
                raise Refused('terminal_case_export_mismatch')
        return 'terminal_failed_before_checkpoint', {'run_id': state['run_id'], 'state_sha256': sha(directory / 'private/state.json'),
            'build_id': state['manifest']['build_id'],
            'export_sha256': sha(archive), 'outcome': 'terminal_failed_before_checkpoint', 'failure': failure,
            'reference_manifest_sha256': reference['reference_manifest_sha256']}

    @staticmethod
    def corrective_case(config):
        authorized = config.get('CorrectiveCase')
        if not authorized: return None
        run_id = authorized.get('run_id', '')
        if not re.fullmatch(r'retest-[0-9a-f]{32}', run_id): raise Refused('corrective_case_identity_invalid')
        old = Path(config['EvidenceRoot']) / run_id / 'private/state.json'
        recovery = Path(authorized['recovery_file'])
        if sha(old) != authorized['state_sha256'] or sha(recovery) != authorized['recovery_sha256']:
            raise Refused('corrective_case_retained_evidence_changed')
        state, receipt = read_json(old), read_json(recovery)
        if (state.get('run_id') != run_id or not state.get('campaign_case') or
            receipt.get('run_id') != run_id or receipt.get('comparison', {}).get('result') != 'vanilla_campaign_unchanged' or
            receipt.get('reference_manifest_sha256') != state['protection']['reference_manifest_sha256'] or
            receipt.get('retained_evidence_sha256', {}).get(str(old)) != authorized['state_sha256']):
            raise Refused('corrective_case_safety_not_established')
        if not state.get('finished'):
            Run.verify_interrupted_recovery(config, state, receipt, old)
            outcome = 'interrupted_failed_create_preserved_stopped_comparison_unchanged'
        else: outcome = 'failed_preserved_exact_comparison_unchanged'
        return {'run_id': run_id, 'state_sha256': authorized['state_sha256'], 'recovery_sha256': authorized['recovery_sha256'],
            'outcome': outcome}

    @staticmethod
    def verify_interrupted_recovery(config, state, receipt, old):
        """Explicit hashed recovery proves a fresh case safe, never normal exit."""
        protection.require_stopped()
        case, comparison = state['campaign_case'], receipt.get('comparison', {})
        process = state.get('process', {})
        namespace = state.get('namespace_id', '')
        reference = Path(state['protection']['reference_directory']) / protection.MANIFEST
        if (receipt.get('classification') != 'interrupted_failed_create_preserved' or
            receipt.get('stopped_before_and_after') is not True or receipt.get('normal_exit_observed') is not False or
            receipt.get('original_case_modified') is not False or receipt.get('source_case_finished') is not False or
            receipt.get('runtime_B_passed') is not False or case.get('phase') != 'create' or case.get('launches') or
            not case.get('failure') or receipt.get('build_id') != state['manifest']['build_id'] or
            not re.fullmatch(r'[0-9a-f]{64}', namespace) or receipt.get('namespace_id') != namespace or
            any(receipt.get('process', {}).get(key) != process.get(key) or not process.get(key)
                for key in ('pid', 'process_created', 'instance_id')) or
            any(comparison.get(key) != 0 for key in ('added', 'removed', 'modified')) or
            not comparison.get('baseline_campaign_files') or comparison['baseline_campaign_files'] != comparison.get('current_campaign_files') or
            receipt.get('comparison_details', {}).get('differences') != [] or sha(reference) != receipt['reference_manifest_sha256']):
            raise Refused('interrupted_corrective_case_safety_not_established')
        retained = receipt.get('retained_evidence_sha256', {})
        expected_evidence = {str(path) for path in old.parent.parent.rglob('*') if path.is_file()} | {str(reference)}
        if set(retained) != expected_evidence or any(sha(path) != digest for path, digest in retained.items()):
            raise Refused('interrupted_corrective_case_retained_evidence_changed')
        original_config = read_json(old.parent / 'config.json')
        if any(original_config.get(key) != config.get(key) for key in ('APRoot', 'SteamAppRoot', 'SteamAccount32', 'LocalProviderRoot')):
            raise Refused('interrupted_corrective_case_namespace_roots_changed')
        ap_root = Path(config['APRoot']) / namespace
        steam_root = Path(config['SteamAppRoot']) / 'remote' / ('ap-' + namespace[:40])
        expected_namespace = {ap_root / name for name in ('campaign.contract', 'owner.lock', 'session.manifest')}
        expected_namespace.add(steam_root / ('sentinel-owner-' + namespace + '.txt'))
        observed = {path for root in (ap_root, steam_root) for path in root.rglob('*') if path.is_file()}
        inventory = receipt.get('namespace_files', [])
        if (observed != expected_namespace or {Path(row['path']) for row in inventory} != expected_namespace or len(inventory) != 4 or
            any(Path(row['path']).stat().st_size != row['size'] or sha(row['path']) != row['sha256'] for row in inventory)):
            raise Refused('interrupted_corrective_case_namespace_payload_or_change')
        protection.require_stopped()

    def __init__(self, config_path, stage, scenario=None, resume_case=None):
        self.config_path = Path(config_path)
        current = load_config(self.config_path)
        current['Scenario'] = scenario or current.get('Scenario', 'A')
        self.reference = Path(current["ActiveRun"])
        if resume_case:
            if stage != 'RUN' or current['Scenario'] != 'B' or not re.fullmatch(r'retest-[0-9a-f]{32}', resume_case):
                raise Refused('invalid_explicit_campaign_case')
            self.directory = Path(current['EvidenceRoot']) / resume_case
            self.config = read_json(self.directory / 'private/config.json')
            self.state = read_json(self.directory / 'private/state.json')
            if self.config != current or self.state['run_id'] != resume_case or not self.state.get('campaign_case'):
                raise Refused('campaign_case_configuration_mismatch')
            if self.state['campaign_case']['phase'] == 'completed': raise Refused('campaign_case_already_completed')
            lifecycle, _ = self.campaign_lifecycle(self.state, self.directory)
            if lifecycle.startswith('terminal_'): raise Refused('terminal_case_preserved_use_ordinary_RUN')
            protection.require_stopped()
            # Only the exact operator-selected case may continue. No identity,
            # reservation, native payload or previous evidence is recreated.
            self.state['finished'] = False
            return
        if stage in ("START", "RUN", "PREPARE"):
            corrective = self.corrective_case(current)
            completed_reference = False
            prior = list(current.get("PriorRuns", []))
            campaign_reference = None
            if self.reference.exists():
                previous = read_json(self.reference)
                previous_directory = Path(previous["run_directory"])
                if previous_directory.parent != Path(current["EvidenceRoot"]) or previous_directory.name != previous["run_id"]:
                    raise Refused("previous_exact_run_reference_mismatch")
                prior.append(str(previous_directory))
                previous_state = read_json(previous_directory / 'private/state.json')
                campaign_reference = previous.get('campaign_reference')
                if previous_state.get('campaign_case'): campaign_reference = previous
                if campaign_reference:
                    campaign_directory = Path(current['EvidenceRoot']) / campaign_reference['run_id']
                    previous_state = read_json(campaign_directory / 'private/state.json')
                    lifecycle, terminal = self.campaign_lifecycle(previous_state, campaign_directory)
                    if terminal: corrective = terminal
                else: lifecycle = 'no_campaign'
                if stage == 'RUN' and lifecycle not in ('no_campaign', 'completed_B', 'terminal_failed_before_checkpoint'):
                    if not corrective or corrective['run_id'] != campaign_reference['run_id']:
                        raise Refused('pending_campaign_case_requires_explicit_ResumeCase_' + campaign_reference['run_id'])
                completed_reference = True
            self.config = current
            self.directory = Path(current["EvidenceRoot"]) / ("retest-" + uuid.uuid4().hex)
            self.directory.mkdir(parents=True)
            (self.directory / "private").mkdir()
            self.state = {"schema": "sentinel-a-retest-private-v1", "run_id": self.directory.name,
                          "started_utc": utc(), "finished": False, "stages": [], "commands": [],
                          "captures": 0, "process": None, "descriptor": None,
                          "prior_runs": list(dict.fromkeys(prior)), "preparation": {"state": "not_performed"},
                          "primary_failure": None, "secondary_failures": [],
                          "debug_capture": {"state": "unavailable", "reason": "optional_process_filtered_capture_not_configured"}}
            if corrective:
                self.state['corrects_case'] = corrective
                self.state['prior_runs'] = list(dict.fromkeys([*self.state['prior_runs'], str(Path(current['EvidenceRoot']) / corrective['run_id'])]))
            write_json(self.directory / "private/config.json", current, create=True)
            self.reference.parent.mkdir(parents=True, exist_ok=True)
            active = {"run_directory": str(self.directory), "run_id": self.directory.name}
            if stage == 'PREPARE' and campaign_reference:
                active['campaign_reference'] = {'run_id': campaign_reference['run_id']}
            write_json(self.reference, active, create=not completed_reference)
            self.save()
        else:
            reference = read_json(self.reference)
            self.directory = Path(reference["run_directory"])
            if self.directory.parent != Path(current["EvidenceRoot"]) or self.directory.name != reference["run_id"]:
                raise Refused("active_run_reference_mismatch")
            self.config = read_json(self.directory / "private/config.json")
            self.state = read_json(self.directory / "private/state.json")
            if self.state["run_id"] != reference["run_id"] or (self.state["finished"] and stage != "EXPORT"):
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

    def fail(self, error, stage):
        failure = safe_failure(error, self.config, stage)
        key = {k: failure.get(k) for k in ('operation', 'stage', 'reason', 'distinction', 'win32_error')}
        known = [self.state.get('primary_failure'), *self.state.get('secondary_failures', [])]
        for item in known:
            if item and all(item.get(k) == value for k, value in key.items()):
                item['occurrences'] = item.get('occurrences', 1) + 1
                self.save()
                return
        private = {**failure, "original_error": str(error), "metadata": getattr(error, "private_metadata", None)}
        write_json(self.prefix("failure-" + stage + '-' + uuid.uuid4().hex[:8]).with_suffix(".private.json"), private, create=True)
        if not self.state.get("primary_failure"):
            self.state["primary_failure"] = failure
        else:
            self.state.setdefault("secondary_failures", []).append(failure)
        self.save()
        print(stage + ": " + failure["reason"])

    def collection(self, operation, prefix, call):
        health = self.state.setdefault('collection_health', {'operations': {}})
        entry = health['operations'].setdefault(operation, {'attempts': 0, 'failures': 0, 'recoveries': 0, 'state': 'not_observed', 'history': []})
        for attempt in range(3):
            entry['attempts'] += 1
            target = prefix.with_name(prefix.name + '-' + str(attempt + 1))
            try:
                value = call(target)
                if entry['state'] == 'unavailable': entry['recoveries'] += 1
                if entry['state'] != 'available':
                    entry['history'].append({'utc': utc(), 'state': 'available', 'attempt': entry['attempts']})
                entry['state'] = 'available'
                self.save()
                return value
            except CollectionUnavailable as error:
                observed, previous = error.observed_identity, self.state.get('process')
                if observed and previous:
                    if (observed.get('pid') is not None and str(observed['pid']) != str(previous['pid'])) or (
                        observed.get('process_created') is not None and str(observed['process_created']) != str(previous['process_created'])):
                        raise Refused('game_process_replaced_while_path_unavailable')
                raw = error.result
                detail = {k: raw.get(k) for k in ('exit_code', 'timed_out', 'cancelled', 'launch_error', 'win32_error', 'duration_ms', 'stdout_truncated', 'stderr_truncated')}
                # Full stderr and exact paths remain in the individual private receipts.
                detail.pop('launch_error', None)
                detail['launch_failed'] = bool(raw.get('launch_error'))
                phases = [line for line in raw.get('stderr', '').splitlines() if line in
                          ('module_enumeration', 'module_disk_hash', 'system_forwarder_disk_hash')]
                detail['operation'] = phases[-1] if phases else error.operation
                if observed and previous:
                    detail['original_process_alive'] = all(observed.get(k) is not None and str(observed[k]) == str(previous[k]) for k in ('pid', 'process_created'))
                detail['private_receipt'] = target.name + '.result.private.json'
                entry['failures'] += 1
                entry['last_failure'] = detail
                if entry['state'] != 'unavailable':
                    entry['history'].append({'utc': utc(), 'state': 'unavailable', 'attempt': entry['attempts'], **detail})
                entry['history'] = entry['history'][-64:]
                entry['state'] = 'unavailable'; self.save()
                if attempt < 2: time.sleep(attempt + 1)
        return None  # Unknown collection state never means process exit or replacement.

    def observe_identity(self, prefix):
        return self.collection('process_identity', prefix, lambda target: observe_game(self.config, target))

    def helper(self, label, script, arguments, timeout=180):
        command = [self.config["Python"], "-B", str(Path(self.config["CandidateManifest"]).parent / script), *arguments]
        record, raw, response = self.execute(label, command, timeout)
        if record.get("launch_error"):
            raise protection.Refused(label + " command launch failed", stage=label, distinction="helper_runtime_unavailable_see_private_OS_error",
                                     win32_error=record.get("win32_error"))
        if record["exit_code"] != 0 or record["stdout_truncated"]:
            failure = None
            for line in (raw["stderr"] + "\n" + raw["stdout"]).splitlines():
                try:
                    value = json.loads(line)
                    if isinstance(value, dict) and value.get("reason"): failure = value; break
                except ValueError:
                    pass
            if failure:
                record["subprocess_failure"] = {k: failure.get(k) for k in
                    ("operation", "stage", "reason", "distinction", "win32_error", "errno", "comparison", "metadata_summary")}
                self.save()
                error = protection.Refused(failure["reason"], stage=failure.get("stage", label),
                    distinction=failure.get("distinction"), comparison=failure.get("comparison"), win32_error=failure.get("win32_error"))
                if failure.get("metadata_summary"): error.summary = failure["metadata_summary"]
                raise error
            if response and response.get("result") == "vanilla_campaign_changed":
                record["response"] = response
                return response
            raise Refused(label + ("_timeout" if record["timed_out"] else "_subprocess_exit_" + str(record["exit_code"])) + "_see_private_output")
        if not isinstance(response, dict): raise Refused(label + "_invalid_JSON_receipt")
        return response

    def source_arguments(self, reference):
        return ["--steam-account", str(self.config["SteamAccount32"]), "--steam-app-root", self.config["SteamAppRoot"],
                "--local-provider-root", self.config["LocalProviderRoot"], "--backup-directory", str(reference)]

    def compare_reference(self, reference, label):
        return self.helper(label, "compare_vanilla_campaign.py", self.source_arguments(reference) +
                           ["--diagnostic-file", str(self.directory / "private" / (label + ".private.json"))])

    def recover_previous(self):
        queue = list(self.state.get("prior_runs", [])); seen = set(); outcomes = []
        records = []; recovered = {}
        while queue:
            directory = Path(queue.pop(0))
            if str(directory) in seen: continue
            seen.add(str(directory))
            if directory.parent != Path(self.config["EvidenceRoot"]) or not re.fullmatch(r"retest-[0-9a-f]{32}", directory.name):
                raise Refused("prior_run_reference_outside_evidence_root")
            previous = read_json(directory / "private/state.json")
            config = read_json(directory / "private/config.json")
            if any(config[k] != self.config[k] for k in ("SteamAccount32", "SteamAppRoot", "LocalProviderRoot", "OriginalBackupDirectory")):
                raise Refused("prior_run_account_or_source_identity_conflict")
            queue.extend(previous.get("prior_runs", []))
            records.append((directory, previous, config))
            for item in previous.get("previous_comparisons", []):
                if item.get("recovered_comparison"):
                    recovered[item["run_id"]] = (item["state_sha256"], item["recovered_comparison"])
        for directory, previous, config in records:
            reference = previous.get("protection", {}).get("reference_directory") or config["OriginalBackupDirectory"]
            if previous.get("protection") and sha(Path(reference) / protection.MANIFEST) != previous["protection"]["reference_manifest_sha256"]:
                raise Refused("previous_protection_reference_manifest_changed")
            comparison = previous.get("comparison") or {}
            if comparison.get("state") != "completed" and directory.name in recovered:
                expected_hash, result = recovered[directory.name]
                if expected_hash != sha(directory / "private/state.json"):
                    raise Refused("previous_recovery_evidence_identity_mismatch")
                comparison = {"state": "completed", "response": result, "reason": "retained_recovered_comparison"}
            outcome = {"run_id": directory.name, "retained_comparison": comparison, "state_sha256": sha(directory / "private/state.json")}
            may_have_run = bool(previous.get("descriptor") or previous.get("process") or previous.get("handoff_armed"))
            prior_result = comparison.get("response", {}).get("result")
            if prior_result == "vanilla_campaign_changed":
                outcome["distinction"] = "unresolved_previous_campaign_regression"
            elif prior_result == "vanilla_campaign_unchanged":
                outcome["distinction"] = "previous_post_test_comparison_retained"
            elif may_have_run:
                result = self.compare_reference(reference, "recover-" + directory.name)
                outcome.update(recovered_comparison=result, distinction="delayed_previous_test_comparison")
                if result["result"] == "vanilla_campaign_changed":
                    outcome["distinction"] = "unresolved_previous_campaign_regression"
            else:
                outcome["distinction"] = "preflight_only_native_not_tested"
            outcomes.append(outcome)
            self.state["previous_comparisons"] = outcomes
            self.save()
            if outcome["distinction"] == "unresolved_previous_campaign_regression":
                raise protection.Refused("previous protected campaign regression remains unresolved", stage="previous_comparison",
                    distinction="potential_previous_test_effect_preserve_both_states_before_new_reference",
                    comparison=outcome.get("recovered_comparison") or comparison.get("response"))
            # A previous published descriptor is retired only with exact retained bytes
            # and stopped game/Steam; a stale file alone never grants a new lease.
            publication = Path(self.config["GameInstall"]) / "sentinel-prelaunch.txt"
            handoff_name = previous.get('handoff_record', 'handoff.private.json')
            if handoff_name not in ('handoff.private.json', 'handoff-create.private.json', 'handoff-resume.private.json'):
                raise Refused('previous_handoff_record_mismatch')
            private_handoff = directory / 'private' / handoff_name
            if publication.exists() and private_handoff.exists():
                retained = read_json(private_handoff)["text"]
                if publication.read_text(encoding="utf-8") == retained:
                    publication.unlink()
            if not self.state.get("reuse_reference") and previous.get("protection"):
                self.state["reuse_reference"] = reference
        write_json(self.directory / "private/previous-comparisons.private.json", outcomes, create=True)

    def prepare(self, activate=True):
        print("DOOM e Steam precisam estar fechados para capturar e verificar os arquivos sem escritores concorrentes.")
        self.state["manifest"] = validate_candidate(self.config)
        self.state["manifest_sha256"] = sha(self.config["CandidateManifest"])
        self.save()
        protection.require_stopped()
        self.recover_previous()
        params = self.source_arguments(self.config["OriginalBackupDirectory"])
        params += ["--ap-root", self.config["APRoot"], "--diagnostic-file", str(self.directory / "private/protection.private.json")]
        for root in self.config["UninstallRoots"]: params.extend(["--uninstall-root", root])
        if self.state.get("reuse_reference"): params.extend(["--reference-directory", self.state["reuse_reference"]])
        receipt = self.helper("run_protection", "prepare_vanilla_backup.py", ["prepare", *params])
        self.state["protection"] = receipt
        self.state["preparation"] = {"state": "protected", "historical_comparison": receipt.get("historical_comparison"),
                                     "reference_manifest_sha256": receipt["reference_manifest_sha256"], "reused": receipt["reused"]}
        self.save()
        if not activate: return
        # Installing diagnostic DLLs and ordinary read-only IPC do not require protection.
        # AP handoff does require it, plus this exact installed pair and game identity.
        validate_candidate(self.config, installed=True)
        if os.environ.get("SENTINEL_AP_TEST_SESSION"):
            raise Refused("prelaunch_conflicting_environment_descriptor")
        root = protection.explicit_path(self.config["APRoot"])
        root.mkdir(exist_ok=True)
        if self.config['Scenario'] == 'B':
            self.prepare_campaign_descriptor(root)
            return
        seed = "sentinel-a-" + self.state["run_id"]
        descriptor = root / (seed + ".txt")
        text = "sentinel-test-session-v1\nseed_hex=" + seed.encode().hex() + "\nteam=0\nslot=1\ngeneration_fingerprint=" + uuid.uuid4().hex + uuid.uuid4().hex + "\nprovenance=synthetic-fixture\nroot=" + str(root) + "\n"
        with descriptor.open("x", encoding="utf-8", newline="\n") as stream: stream.write(text)
        probe = Path(self.config["CandidateManifest"]).parent / "sentinel_probe.exe"
        record, raw, response = self.execute("namespace_prepare", [str(probe), "--save-session-prepare", str(descriptor)], 20)
        if record["exit_code"] != 0 or not isinstance(response, dict) or not re.fullmatch("[0-9a-f]{64}", response.get("namespace_id", "")):
            if isinstance(response, dict): record["response"] = scalars(response, ("result", "outcome", "win32_error"))
            self.save()
            raise Refused("namespace_prepare_refused_" + json.dumps(record.get("response", {}), sort_keys=True))
        self.state.update(descriptor=str(descriptor), namespace_id=response["namespace_id"])
        self.state["preparation"]["state"] = "prepared"
        self.save()

    def discover_recorded_process(self):
        try:
            return self._discover_recorded_process()
        except (MemoryError, OSError) as error:
            self.collection_degraded(error)
            return False

    def _discover_recorded_process(self):
        """Recover a short-lived attempt by exact control hash, never newest file/PID."""
        root = Path(os.environ.get("LOCALAPPDATA", "")) / "SentinelCore/diagnostics"
        if not root.is_dir(): return False
        earliest = int(datetime.datetime.fromisoformat(self.state["started_utc"]).timestamp() * 10000000) + 116444736000000000
        matches = {}
        for index, path in enumerate(root.iterdir()):
            if index >= 4096: raise Refused("automatic_record_scan_limit_exact_identity_not_resolved")
            match = re.fullmatch(r"([0-9]+)-([0-9]+)\.(jsonl|latest\.json)", path.name)
            if not match or int(match[2]) < earliest: continue
            try:
                candidates = [startup_latest(path)] if match[3] == 'latest.json' else startup_records(path, {})
                for value in candidates:
                    if (isinstance(value, dict) and value.get('schema') == 'sentinel-startup-v1' and value.get("control_sha256") == self.state.get("control_sha256") and
                    value.get("build_id") == self.state["manifest"]["build_id"] and str(value.get("pid")) == match[1] and str(value.get("process_created")) == match[2]):
                        matches[(match[1], match[2])] = {"pid": int(match[1]), "process_created": match[2], "path": None, "modules": [], "source": "automatic_record_only"}
            except (ValueError, UnicodeError): continue
        if len(matches) > 1: raise Refused("multiple_process_records_for_this_run_no_identity_guess")
        if not matches: return False
        self.state["process"] = next(iter(matches.values())); self.save()
        return True

    def prepare_campaign_descriptor(self, root):
        options = self.state['manifest'].get('milestone_b_options')
        if (not isinstance(options, dict) or options.get('provenance') != 'synthetic-fixture' or
            options.get('campaign') != 'base' or options.get('starting_stage') != 'base_start' or
            type(options.get('difficulty')) is not int or options['difficulty'] not in range(4)):
            raise Refused('immutable_generated_slot_options_required')
        seed = 'sentinel-b-' + self.state['run_id']
        generated = {**options, 'seed': seed, 'team': 0, 'slot': 1}
        source = self.directory / 'private/generated-slot.private.json'
        write_json(source, generated, create=True)
        fingerprint = sha(source)
        base = ('sentinel-test-session-v2\nseed_hex=' + seed.encode().hex() + '\nteam=0\nslot=1\ngeneration_fingerprint=' +
                fingerprint + '\nprovenance=synthetic-fixture\nroot=' + str(root) + '\ncampaign=base\nstarting_stage=base_start\ndifficulty=' +
                str(options['difficulty']) + '\nintent=')
        descriptors = {}
        for phase in ('create', 'resume'):
            path = root / (seed + '-' + phase + '.txt')
            with path.open('x', encoding='utf-8', newline='\n') as stream: stream.write(base + phase + '\n')
            descriptors[phase] = {'path': str(path), 'sha256': sha(path)}
        self.state['campaign_case'] = {'phase': 'create', 'options': options, 'generation_fingerprint': fingerprint,
            'descriptors': descriptors, 'launches': [], 'runtime_proof': 'pending_maintainer_smoke'}
        self.state['descriptor'] = descriptors['create']['path']
        self.save()
        probe = Path(self.config['CandidateManifest']).parent / 'sentinel_probe.exe'
        record, _, response = self.execute('namespace_prepare', [str(probe), '--save-session-prepare', self.state['descriptor']], 20)
        if record['exit_code'] != 0 or not isinstance(response, dict) or not re.fullmatch('[0-9a-f]{64}', response.get('namespace_id', '')):
            raise Refused('campaign_namespace_prepare_refused')
        self.state['namespace_id'] = response['namespace_id']
        self.state['preparation']['state'] = 'prepared'
        self.save()

    def campaign_progress(self):
        case = self.state['campaign_case']
        records = automatic_rows(self.state.get('automatic_log', {}))
        current = next((row['campaign'] for row in reversed(records) if row.get('campaign', {}).get('enabled')), {})
        failure = self.campaign_failure(records, case['options'])
        if failure:
            if not case.get('failure'):
                case['failure'] = failure
                case['runtime_proof'] = 'failed_observing_until_normal_exit'
                self.fail(Refused(failure['fault'] + '_' + str(failure['reason'])), failure['fault'])
                print('Teste B falhou. Feche DOOM e Steam normalmente. A coleta segura continua; nao avance para outro checkpoint ou segundo lancamento.')
                self.save()
            return current
        if not current: return {}
        if case.get('failure'): return current
        if current.get('continuity_persisted') and not current.get('resumed') and current.get('phase') == 'checkpoint_saved':
            message = 'Checkpoint nativo confirmado e verificado. Pode fechar DOOM e Steam normalmente; a segunda abertura usara a mesma identidade.'
        elif current.get('resumed') and current.get('source_verified') and current.get('parser_completed') and current.get('map_active'):
            message = 'Checkpoint correspondente reaberto pelo caminho nativo. Confirme controle jogavel, dificuldade e configuracoes; depois feche DOOM e Steam.'
        else: return current
        if case.get('prompt_phase') != case['phase']:
            print(message); case['prompt_phase'] = case['phase']; self.save()
        return current

    def complete_campaign_launch(self):
        case = self.state['campaign_case']; phase = case['phase']
        current = self.campaign_progress()
        self.compare_campaign_launch()
        if case.get('failure'):
            case['runtime_proof'] = 'failed_closed_and_compared'
            self.save()
            raise Refused('campaign_failed_preserved_do_not_recreate')
        records = automatic_rows(self.state.get('automatic_log', {}))
        admission = records[-1].get('admission', {}) if records else {}
        valid = (current.get('effective_difficulty') == case['options']['difficulty'] and
            self.state.get('capture_health', {}).get('module_state') == 'verified' and self.state.get('process', {}).get('instance_id') and
            admission.get('state') == 3 and admission.get('fault') == 0 and admission.get('flags', 0) & 7 == 7 and
            admission.get('prepared_routes') == admission.get('required_routes') == 63 and admission.get('namespace_id') == self.state['namespace_id'])
        if phase == 'create':
            valid = valid and all(current.get(k) for k in ('native_saved', 'readback_verified', 'continuity_persisted', 'native_factory_matched'))
        else:
            first = case['launches'][0]['campaign']
            valid = valid and all(current.get(k) for k in ('resumed', 'source_verified', 'parser_completed', 'map_active'))
            valid = valid and current.get('source_checkpoint') == first['checkpoint'] and current.get('map') == first['map'] and current.get('loaded_difficulty') == case['options']['difficulty']
            previous = case['launches'][0]['process']; observed = self.state['process']
            valid = valid and observed['process_created'] != previous['process_created'] and observed['instance_id'] != previous['instance_id']
        if not valid: raise Refused('campaign_' + phase + '_proof_incomplete_do_not_recreate')
        launch = {'phase': phase, 'campaign': current, 'protection_manifest_sha256': self.state['protection']['reference_manifest_sha256'],
            'process': scalars(self.state['process'], ('pid', 'process_created', 'instance_id')),
            'automatic_startup': self.state.get('automatic_log'), 'comparison': self.state['comparison']}
        record = self.directory / ('private/campaign-' + phase + '.private.json')
        private_launch = {**launch, 'full_process': self.state['process'], 'protection': self.state['protection']}
        if record.exists():
            if read_json(record) != private_launch: raise Refused('retained_campaign_launch_evidence_conflict')
        else: write_json(record, private_launch, create=True)
        case['launches'].append(launch)
        case['phase'] = 'prepare_resume' if phase == 'create' else 'completed'
        self.save()

    def compare_campaign_launch(self):
        protection.require_stopped()
        reference = self.state['protection']
        if sha(Path(reference['reference_directory']) / protection.MANIFEST) != reference['reference_manifest_sha256']:
            raise Refused('run_reference_manifest_changed')
        response = self.compare_reference(reference['reference_directory'], 'campaign_' + self.state['campaign_case']['phase'] + '_comparison')
        self.state['comparison'] = {'state': 'completed', 'response': response}
        self.save()
        if response['result'] != 'vanilla_campaign_unchanged': raise Refused('campaign_original_regression')

    def run_campaign(self):
        if not self.state.get('campaign_case'): self.prepare()
        case = self.state['campaign_case']
        # A resumed shell observes the exact retained attempt first; it cannot
        # turn an ambiguous native create into a retry of New Game.
        if self.state.get('handoff_armed') and case['phase'] in ('create', 'resume'):
            if not self.state.get('process') and not self.discover_recorded_process():
                raise Refused('interrupted_campaign_attempt_not_observed_do_not_recreate')
            self.collect_startup_log()
            self.complete_campaign_launch()
            self.state.pop('handoff_armed', None)
        while case['phase'] != 'completed':
            if sha(self.directory / 'private/generated-slot.private.json') != case['generation_fingerprint']:
                raise Refused('generated_slot_options_changed')
            if case['phase'] == 'prepare_resume':
                params = self.source_arguments(self.config['OriginalBackupDirectory'])
                params += ['--ap-root', self.config['APRoot'], '--reference-directory', self.state['protection']['reference_directory'],
                    '--diagnostic-file', str(self.directory / 'private/second-protection.private.json')]
                for root in self.config['UninstallRoots']: params += ['--uninstall-root', root]
                self.state['protection'] = self.helper('second_launch_protection', 'prepare_vanilla_backup.py', ['prepare', *params])
                self.state['process'] = None
                for key in ('automatic_log', 'capture_health', 'profile_failure_reported', 'comparison', 'handoff_armed'):
                    self.state.pop(key, None)
                case['phase'] = 'resume'; self.save()
            descriptor = case['descriptors'][case['phase']]
            if sha(descriptor['path']) != descriptor['sha256']: raise Refused('campaign_descriptor_changed')
            validate_candidate(self.config, installed=True)
            self.state['descriptor'] = descriptor['path']; self.save()
            instruction = ('Protecao pronta. Internet ligada: abra Steam e DOOM. Escolha New Game na campanha Base, sem escolher dificuldade; a sala define o valor. '
                'Jogue ate o primeiro checkpoint e aguarde a confirmacao abaixo antes de sair.' if case['phase'] == 'create' else
                'Segunda protecao pronta. Abra Steam e DOOM novamente e escolha Continue no mesmo slot AP. Confirme que o checkpoint e jogavel.')
            self.observe_launch(prepare=False, instruction=instruction)
            self.complete_campaign_launch()
            self.state.pop('handoff_armed', None)
        case['runtime_proof'] = 'native_create_save_reopen_observed_playability_requires_operator_confirmation'
        self.save()

    def collect_startup_log(self):
        try:
            self._collect_startup_log()
        except (MemoryError, OSError) as error:
            # Optional collection never discards the last complete observation
            # or interrupts waiting for stopped-process comparison/export.
            self.collection_degraded(error)

    def collection_degraded(self, error):
        self.state['collection_degradation'] = type(error).__name__
        try: self.save()
        except (MemoryError, OSError): pass

    def _collect_startup_log(self):
        process = self.state.get("process")
        if not process: return
        path = Path(os.environ["LOCALAPPDATA"]) / "SentinelCore/diagnostics" / (str(process["pid"]) + "-" + str(process["process_created"]) + ".jsonl")
        previous = self.state.get('automatic_log', {})
        status = {'truncated': previous.get('truncated', False), 'latest_state': 'unavailable'}
        def validated(item):
            if (item.get('schema') != 'sentinel-startup-v1' or str(item.get('pid')) != str(process['pid']) or
                str(item.get('process_created')) != str(process['process_created']) or item.get('build_id') != self.state['manifest']['build_id']):
                raise ValueError('process_or_build_identity_mismatch')
            actual = item.get('admission', {}).get('namespace_id')
            expected = self.state.get('namespace_id')
            if expected and actual and actual != expected and not (actual == '0' * 64 and item.get('admission', {}).get('state') == 0):
                raise ValueError('namespace_identity_mismatch')
            if self.state.get('control_sha256') and item.get('control_sha256') and item['control_sha256'] != self.state['control_sha256']:
                raise ValueError('control_identity_mismatch')
            return safe_startup_record(item)
        rows = []
        try:
            for item in startup_records(path, status):
                if len(rows) == 128:
                    status['truncated'] = True
                    status['history_record_limit'] = True
                    break
                try: rows.append(validated(item))
                except (ValueError, TypeError, AttributeError): status['rejected_records'] = status.get('rejected_records', 0) + 1
        except (MemoryError, OSError) as error:
            status['history_state'] = type(error).__name__
        # A transient failed/truncated read cannot erase records already retained.
        if len(rows) < len(previous.get('records', [])): rows = previous['records']
        latest = previous.get('latest')
        try:
            candidate = validated(startup_latest(path.with_suffix('.latest.json')))
            prior = latest or (rows[-1] if rows else {})
            if (candidate.get('at_ms', 0) < prior.get('at_ms', 0) or
                candidate.get('b_diagnostics', {}).get('sequence', 0) < prior.get('b_diagnostics', {}).get('sequence', 0)):
                raise ValueError('stale_snapshot')
            latest = candidate
            status['latest_state'] = 'captured'
        except FileNotFoundError: pass
        except (ValueError, TypeError, AttributeError, UnicodeError) as error:
            status.update(latest_state='rejected', latest_reason=str(error) if str(error) in
                ('process_or_build_identity_mismatch', 'namespace_identity_mismatch', 'control_identity_mismatch', 'stale_snapshot', 'snapshot_oversized', 'snapshot_not_object') else 'malformed_snapshot')
        except (MemoryError, OSError) as error:
            status['latest_state'] = type(error).__name__
        self.state['automatic_log'] = {'state': 'captured' if rows or latest else 'unavailable', **status, 'records': rows}
        if latest:
            self.state['automatic_log']['latest'] = latest
            last = rows[-1].get('b_diagnostics', {}).get('sequence', 0) if rows else 0
            status['sequence_gap'] = max(0, latest.get('b_diagnostics', {}).get('sequence', 0) - last - 1)
            self.state['automatic_log']['sequence_gap'] = status['sequence_gap']
        rows = automatic_rows(self.state['automatic_log'])
        causes = [r.get('b_diagnostics', {}).get('first_failure') for r in rows]
        causes.append(previous.get('first_failure'))
        causes = [cause for cause in causes if cause and cause.get('sequence')]
        if causes: self.state['automatic_log']['first_failure'] = min(causes, key=lambda cause: cause['sequence'])
        if self.state.get('campaign_case'): self.campaign_progress()
        profile_failure = next((row["profile"] for row in rows
            if row["profile"].get("first_failed_stage") not in (None, "none") and row["profile"].get("request_id")), None)
        if profile_failure and not self.state.get("profile_failure_reported"):
            self.state["profile_failure_reported"] = True
            stage = profile_failure["first_failed_stage"]
            predicate = profile_failure["first_failure"].get("predicate", "predicate_unavailable")
            label = 'native PROFILE after admission: ' if stage == 'write_after_refusal' else 'native PROFILE initialization: '
            self.fail(protection.Refused(label + stage + ": " + predicate,
                stage="profile_" + stage, distinction=predicate), "native_profile")
        target = self.directory / "private/automatic-startup.private.json"
        write_json(target, self.state["automatic_log"], create=not target.exists())
        self.save()
        if not self.state.get("primary_failure"):
            failure = next((row["installation"]["primary_failure"] for row in rows if row["installation"]["primary_failure"]), None)
            if failure:
                startup = failure.get('stage') in (19, 'startup')
                self.fail(protection.Refused("retained native startup failure in automatic startup record" if startup else "retained native installation failure in automatic startup record", stage="native_startup" if startup else "native_installation",
                    distinction="original_stage_and_reason_in_automatic_installation_record", win32_error=failure.get("win32_error")), "automatic_startup")

    def run(self):
        if self.config['Scenario'] == 'B': return self.run_campaign()
        return self.observe_launch()

    def observe_launch(self, prepare=True, instruction=None):
        handoff = None
        try:
            if prepare: self.prepare()
            protection.require_stopped()
            handoff = Handoff(self)
            print(instruction or "Protecao pronta. Abra Steam e DOOM normalmente, fique no menu e depois feche DOOM e Steam. A coleta e automatica; Ctrl+C exporta o que estiver disponivel.")
            launch = self.state.get('campaign_case', {}).get('phase', '')
            deadline = time.monotonic() + self.config.get("DiscoverySeconds", 180)
            attempt = 0
            while True:
                attempt += 1
                try:
                    observed = self.observe_identity(self.directory / "private" / ("discovery-" + launch + str(attempt)))
                    if observed:
                        self.state["process"] = {**observed, "instance_id": None}
                        self.save()
                        break
                except Refused as error:
                    if str(error) != "no_game_process": raise
                    if self.discover_recorded_process(): break
                if time.monotonic() >= deadline: raise Refused("game_discovery_deadline_no_game_observed")
                time.sleep(1)
            deadline = time.monotonic() + self.config.get("ObservationSeconds", 1800)
            captures = 0; failure_captures = 0; last_log = None; next_capture = 0
            while self.state["process"].get("source") != "automatic_record_only":
                try: self.collect_startup_log()
                except (OSError, ValueError) as error: self.fail(error, 'automatic_log_collection')
                if launch: self.campaign_progress()
                signature = json.dumps(self.state.get("automatic_log"), sort_keys=True)
                retry_modules = self.state.get('capture_health', {}).get('module_state') != 'verified'
                failed = bool(self.state.get('campaign_case', {}).get('failure'))
                if (captures < 8 or (failed and failure_captures < 8)) and (captures == 0 or signature != last_log or
                        ((retry_modules or failed) and time.monotonic() >= next_capture)):
                    try: self.capture()
                    except (Refused, protection.Refused, OSError, ValueError) as error:
                        self.fail(error, "safe_capture")
                        if 'replaced' in str(error) or 'identity_mismatch' in str(error) or str(error) == 'loaded_module_identity_verified_mismatch': raise
                    captures += 1; last_log = signature
                    if failed: failure_captures += 1
                    next_capture = time.monotonic() + 30
                attempt += 1
                try:
                    observed = self.observe_identity(self.directory / "private" / ("observation-" + launch + str(attempt)))
                    if observed and any(str(observed[k]) != str(self.state["process"][k]) for k in ("pid", "process_created", "path")):
                        raise Refused("game_process_replaced")
                except Refused as error:
                    if str(error) == "no_game_process": break
                    raise
                if time.monotonic() >= deadline: raise Refused("game_close_deadline_process_not_stopped")
                time.sleep(1)
            try: self.collect_startup_log()
            except (OSError, ValueError) as error: self.fail(error, 'automatic_log_collection')
            print("DOOM fechado. Aguardando Steam encerrar para comparar a referencia deste run.")
            deadline = time.monotonic() + self.config.get("CloseSeconds", 180)
            while True:
                try: protection.require_stopped(); break
                except protection.Refused:
                    if time.monotonic() >= deadline: raise Refused("post_test_comparison_waiting_for_Steam_to_exit")
                    time.sleep(1)
        except (Refused, protection.Refused, OSError, ValueError, MemoryError, KeyboardInterrupt) as error:
            self.fail(Refused("operator_interrupted_partial_evidence") if isinstance(error, KeyboardInterrupt) else error, "RUN")
            raise
        finally:
            if handoff:
                try: handoff.retire()
                except (OSError, Refused) as error: self.fail(error, "handoff_cleanup")

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
            observed = self.observe_identity(self.directory / "private" / f"capture-{capture_id}-{index}-process")
            if observed is None: return
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
        final = self.observe_identity(self.directory / "private" / f"capture-{capture_id}-final-process")
        if final and any(str(final[key]) != str(self.state["process"][key]) for key in ("pid", "process_created", "path")):
            raise Refused("game_process_replaced_at_capture_end")
        observation = self.collection('module_collection', self.directory / 'private' / f'capture-{capture_id}-modules',
            lambda target: observe_modules(self.config, self.state['process'], target)) if final else None
        after = self.observe_identity(self.directory / 'private' / f'capture-{capture_id}-modules-process') if observation else None
        if after and any(str(after[key]) != str(self.state['process'][key]) for key in ('pid', 'process_created', 'path')):
            raise Refused('game_process_replaced_during_module_collection')
        module_state, modules = module_roles(self.config, manifest, observation) if observation and after else ('collection_unavailable', [])
        health = self.state.setdefault("capture_health", {"module_history": []})
        health["module_state"] = module_state
        health["module_history"].append({"capture": capture_id, "state": module_state, "observed_modules": len(modules)})
        if modules: self.state["process"]["modules"] = modules
        self.save()
        if module_state == 'verified_mismatch': raise Refused("loaded_module_identity_verified_mismatch")
        if namespace_refusal:
            raise Refused(namespace_refusal)
        for item in self.state["commands"][-len(QUERIES):]:
            failure = item.get("response", {}).get("installation", {}).get("primary_failure")
            if failure:
                startup = failure.get('stage') in (19, 'startup')
                raise protection.Refused(("native startup refused: " if startup else "native installation refused: ") + str(failure.get("reason")), stage="native_startup" if startup else "native_installation",
                    distinction="retained_first_installation_failure_all_safe_queries_collected", win32_error=failure.get("win32_error"))
        for item in self.state["commands"][-len(QUERIES):]:
            if item["query"] == "save_admission" and item.get("response", {}).get("state") in ("rejected", "faulted"):
                raise protection.Refused("AP admission refused: " + str(item["response"].get("fault")), stage="native_admission",
                    distinction="native_session_refusal_all_safe_queries_collected")
        if not self.state.get('campaign_case'):
            print("Consultas seguras coletadas; feche DOOM e Steam normalmente quando concluir a observacao do menu.")

    def finish(self, args):
        if args.stage == "EXPORT" and (self.state.get("comparison") or {}).get("state") == "completed":
            return  # Export cannot reattribute later legitimate changes to a completed test.
        self.state["operator"] = {"attribution": "operator_supplied_not_script_verified", "catalog": args.catalog,
                                  "selection": args.selection, "rollback": args.rollback, "note": redact(args.note, self.config)}
        if self.state.get('campaign_case') and self.state.get('comparison', {}).get('state') == 'completed':
            self.state['finished'] = True
            self.state['finished_utc'] = utc()
            self.save()
            return
        if self.state.get("process") and self.state.get("capture_health", {}).get("module_state") in ('not_yet_observable', 'collection_unavailable'):
            self.fail(Refused("loaded_module_identity_not_established"), "capture_health")
        comparison = {"state": "not_performed", "reason": "game_not_observed_and_no_activation"}
        self.state["comparison"] = comparison
        if self.state.get("protection") and (self.state.get("process") or self.state.get("handoff_armed")):
            try:
                protection.require_stopped()
                reference = self.state["protection"]
                if sha(Path(reference["reference_directory"]) / protection.MANIFEST) != reference["reference_manifest_sha256"]:
                    raise Refused("run_reference_manifest_changed")
                response = self.compare_reference(reference["reference_directory"], "campaign_comparison")
                comparison.update(state="completed", reason="compared_exact_pre_test_reference", response=response)
                if response["result"] == "vanilla_campaign_changed":
                    self.fail(protection.Refused("protected campaign changed since this run preparation", stage="campaign_comparison",
                        distinction="unexplained_campaign_regression_no_automatic_baseline_refresh", comparison=response), "campaign_comparison")
            except (Refused, protection.Refused, OSError, ValueError, KeyboardInterrupt) as error:
                comparison.update(state="not_performed", reason=safe_failure(error, self.config, "campaign_comparison"))
                self.fail(error, "campaign_comparison")
        self.state["finished"] = True
        self.state["finished_utc"] = utc()
        self.save()

    def export(self, checkpoint=None):
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
                modules.append({'basename': name, 'role': module.get('role', 'not_classified'),
                    'verification': module.get('verification', 'not_verified'), 'on_disk_sha256': module.get('sha256')})
        report = {"schema": "sentinel-milestone-a-retest-shareable-v1", "run_id": self.state["run_id"],
            "started_utc": self.state["started_utc"], "finished_utc": self.state.get("finished_utc"),
            "expected": {"build_id": manifest.get("build_id"), "product_version": manifest.get("product_version"),
                         "game_basename": "DOOMEternalx64vk.exe", "game_sha256": manifest.get("supported_game_sha256"),
                         "required_routes": manifest.get("required_routes")},
            "candidate": {"manifest_sha256": self.state.get("manifest_sha256"),
                          "files": [{"basename": name, "sha256": hashes[name]} for name in REQUIRED if name in hashes]},
            "prepared_namespace_id": self.state.get("namespace_id"),
            "preparation": self.state.get("preparation", {"state": "not_performed"}),
            "primary_failure": self.state.get("primary_failure"), "secondary_failures": self.state.get("secondary_failures", []),
            "previous_comparisons": self.state.get("previous_comparisons", []),
            "automatic_startup": self.state.get("automatic_log", {"state": "not_observed"}),
            "capture_health": self.state.get("capture_health", {"module_state": "not_observed", "module_history": []}),
            "collection_health": self.state.get('collection_health', {'operations': {}}),
            "collection_degradation": self.state.get('collection_degradation'),
            "module_evidence_limit": "verified mapped paths and current disk hashes do not attest every loaded byte",
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
        report["admission_observation"] = ("admitted_in_read_only_snapshot" if admitted else
            "native_refusal_observed" if any(i.get("response", {}).get("state") in ("rejected", "faulted") for i in admissions) else
            "NOT_TESTED" if not process else "not_established")
        logged = automatic_rows(report["automatic_startup"])
        trace = next((r["profile"] for r in reversed(logged) if r.get("profile", {}).get("request_id")), {})
        report["profile_initialization"] = trace or {"state": "not_observed"}
        native_failure = next((i.get("response", {}) for i in admissions if i.get("response", {}).get("state") in ("rejected", "faulted")), {})
        if not native_failure:
            native_failure = next((r["admission"] for r in logged if r.get("admission", {}).get("state") in (4, 5)), {})
        report["native_failure"] = {**scalars(native_failure, ("state", "fault")),
            "first_failed_stage": trace.get("first_failed_stage", "not_observed"), "first_failure": trace.get("first_failure", {})} if native_failure else None
        case_options = self.state.get('campaign_case', {}).get('options')
        campaign_failure = (self.campaign_failure(logged, case_options) if case_options else None) or self.state.get('campaign_case', {}).get('failure')
        if campaign_failure:
            profile_time = trace.get('first_failure', {}).get('changed_ms')
            campaign_time = campaign_failure.get('at_ms')
            profile_stage = trace.get('first_failed_stage')
            # Native event times and failed-sample bounds share GetTickCount64
            # for this exact process. Collector invocation order is not evidence.
            before_profile = bool(campaign_time and profile_time and campaign_time < profile_time)
            before_campaign = bool(campaign_failure.get('timing') == 'native_event' and campaign_time and
                profile_time and profile_time < campaign_time and profile_stage != 'write_after_refusal')
            if campaign_failure.get('fault') == 'native_b':
                report['native_failure'] = {'fault': 'native_b', 'first_failed_stage': campaign_failure['first_failed_stage'],
                    'first_failure': campaign_failure['first_failure'], 'ordering': 'first_causal_b_event_exact_process'}
            elif campaign_failure.get('fault') == 'native_startup':
                report['native_failure'] = {'fault': 'native_startup', 'session_state': 'rejected', 'session_fault': 'missed_startup',
                    'first_failed_stage': 'startup', 'first_failure': campaign_failure,
                    'ordering': 'startup_before_profile_and_campaign_parser'}
            elif campaign_failure.get('fault') == 'native_profile':
                report['native_failure'] = {'fault': 'native_profile', 'first_failed_stage': profile_stage,
                    'first_failure': trace.get('first_failure'), 'campaign_state': campaign_failure.get('campaign'),
                    'ordering': 'profile_refusal_after_native_creation' if campaign_failure.get('campaign', {}).get('phase') == 'native_created' else 'profile_refusal'}
            elif before_campaign:
                report['native_failure'] = {'fault': 'native_profile', 'first_failed_stage': profile_stage,
                    'first_failure': trace.get('first_failure'), 'campaign_aftermath': campaign_failure, 'ordering': 'profile_before_campaign'}
            else:
                report['native_failure'] = {'fault': 'native_campaign', 'first_failed_stage': 'campaign_entry_or_checkpoint',
                    'first_failure': campaign_failure, 'profile_aftermath': profile_stage,
                    'ordering': 'campaign_before_profile' if before_profile else 'sampled_order_not_established'}
        if process and logged and report["admission_observation"] == "not_established":
            if logged[-1].get("admission", {}).get("state") in (4, 5): report["admission_observation"] = "native_refusal_in_automatic_record"
        installations = [i.get("response", {}).get("installation", {}) for i in report["commands"] if i["query"] == "save_installation"]
        report["game_observation"] = "observed" if process else "NOT_OBSERVED"
        if process and process.get("source") == "automatic_record_only": report["game_observation"] = "observed_in_automatic_record_only"
        report["hook_installation"] = installations[-1].get("phase", "not_established") if installations else "NOT_TESTED" if not process else "not_established"
        if not installations and any(r.get("installation", {}).get("primary_failure") for r in logged):
            report["hook_installation"] = "native_failure_in_automatic_record"
        retained_installations = installations + [r.get('installation', {}) for r in logged]
        startup_event = next((i.get('primary_failure', {}) for i in retained_installations
            if (i.get('primary_failure') or {}).get('stage') in (19, 'startup')), None)
        hooks_ready = any(i.get('last_completed_stage') in (16, 'ready') and i.get('created', 0) > 0 and
            i.get('enabled') == i.get('created') for i in retained_installations)
        if hooks_ready: report['hook_installation'] = 'READY/PASS'
        report['startup_observation'] = 'FAILED/unobserved' if startup_event else 'not_established'
        case = self.state.get('campaign_case')
        if case:
            report['campaign_case'] = scalars(case, ('phase', 'generation_fingerprint', 'runtime_proof'))
            report['campaign_case'].update(options=case['options'], launches=case['launches'])
            report['campaign_case']['namespace_id'] = self.state.get('namespace_id')
            report['campaign_case']['backup_verified'] = 'not_requested_or_required'
            report['campaign_case']['failure'] = case.get('failure')
            report['campaign_case']['corrects_case'] = self.state.get('corrects_case')
            report['campaign_case']['lifecycle'] = self.campaign_lifecycle(self.state, self.directory, require_export=False)[0]
        summary = ["Milestone " + self.config['Scenario'] + " RUN", "", "Preparation: " + report["preparation"]["state"],
                   "Game: " + report["game_observation"], "Hook installation: " + report["hook_installation"], "AP admission: " + report["admission_observation"],
                   "Startup observation: " + report["startup_observation"], "Expected routes: " + str(report["expected"]["required_routes"]), "Runtime PASS: not claimed.", ""]
        if case:
            summary += ['Campaign case: ' + case['phase'], 'Immutable synthetic slot difficulty: ' + str(case['options']['difficulty']),
                'Native create/save/reopen evidence: ' + case['runtime_proof'], 'Playable checkpoint and settings require maintainer confirmation.']
            if report['campaign_case']['lifecycle'] == 'terminal_failed_before_checkpoint':
                summary.append('Terminal failure before checkpoint. After this final ZIP, ordinary RUN creates a linked new disposable case; the failed case remains preserved. Do not use ResumeCase for it.')
            elif case.get('failure'):
                summary.append('Failed case preserved; no second launch or automatic recreation. Close normally on failure; collection continues until exit or the bounded deadline.')
            elif case['phase'] != 'completed':
                summary.append('Explicit continuation: the same RUN command with -ResumeCase ' + self.state['run_id'] +
                    '; close DOOM and Steam first. Unproved creation is never retried or overwritten.')
        if report["native_failure"]:
            summary.append("Native game failure: " + json.dumps(report["native_failure"], sort_keys=True))
        summary.append("Capture health: " + json.dumps(report["capture_health"], sort_keys=True))
        for operation, status in report['collection_health']['operations'].items():
            summary.append(f"Collection {operation}: {status['state']}; failed attempts={status['failures']}; recoveries={status['recoveries']}.")
        if trace:
            summary.append("PROFILE initialization: " + json.dumps(trace, sort_keys=True))
        if report["primary_failure"]: summary.append("First workflow failure (separate from native cause): " + json.dumps(report["primary_failure"], sort_keys=True))
        if report["preparation"].get("historical_comparison"):
            summary.append("Historical backup comparison: " + json.dumps(report["preparation"]["historical_comparison"], sort_keys=True))
        for stage in report["stages"]:
            if stage.get("failure"):
                summary.append(stage["stage"] + " " + stage["state"] + ": " + stage["failure"])
        for item in admissions:
            response = item.get("response", {})
            summary.append(f"Admission exit={item['exit_code']}; state={response.get('state', 'unavailable')}; fault={response.get('fault', 'unavailable')}; routes={response.get('prepared_routes', 'unknown')}; namespace={item.get('namespace_state', 'unavailable')}.")
        for item in report["commands"]:
            if item.get("subprocess_failure"):
                summary.append("Preflight failure: " + json.dumps(item["subprocess_failure"], sort_keys=True))
            failure = item.get("response", {}).get("installation", {}).get("primary_failure")
            if failure:
                summary.append(("Startup observation failure: " if failure.get("stage") in (19, "startup") else "Installation primary failure: ") + json.dumps(failure, sort_keys=True))
        summary += ["", "Campaign comparison: " + report["comparison"]["state"],
                    "Catalog/selection notes are operator supplied; the script did not verify UI state.",
                    "Optional debug capture: " + report["debug_capture"]["state"],
                    "Raw output/configuration remain private. Only SUMMARY.md and report.json are in this ZIP."]
        encoded = json.dumps(report, indent=2) + "\n"
        summary_text = "\n".join(summary) + "\n"
        suffix = "-" + checkpoint if checkpoint else ""
        share = self.directory / ("shareable" + suffix)
        share.mkdir()
        write_json(share / "report.json", report, create=True)
        (share / "SUMMARY.md").write_text(summary_text, encoding="utf-8")
        zip_path = self.directory / (self.state["run_id"] + suffix + "-shareable.zip")
        with zipfile.ZipFile(zip_path, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.writestr("SUMMARY.md", summary_text)
            bundle.writestr("report.json", encoded)
        if case and not checkpoint:
            lifecycle, terminal = self.campaign_lifecycle(self.state, self.directory)
            if terminal:
                # Retire only after exact close/comparison AND successful final
                # export; preserve every case file and disposable namespace.
                active = read_json(self.reference)
                if active['run_id'] == self.state['run_id']:
                    active['retired_campaign'] = terminal
                    write_json(self.reference, active)
        print("Attach only this sanitized ZIP: " + str(zip_path))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("stage", choices=("RUN", "PREPARE", "EXPORT"))
    parser.add_argument("--catalog", default="not_observed")
    parser.add_argument("--selection", default="not_observed")
    parser.add_argument("--rollback", default="not_performed")
    parser.add_argument("--note", default="")
    parser.add_argument("--comparison-cancelled", action="store_true")
    parser.add_argument('--scenario', choices=('A', 'B'))
    parser.add_argument('--resume-case')
    args = parser.parse_args(argv)
    run = None
    try:
        config = load_config(args.config)
        lock_path = Path(config["ActiveRun"] + ".lock")
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with lock_path.open("xb"): pass
        except FileExistsError: pass
        # An OS-held exclusive handle survives shell boundaries but never a crash.
        with protection.pinned(lock_path):
            run = Run(args.config, args.stage, args.scenario, args.resume_case)
            stage = {"stage": args.stage, "started_utc": utc(), "state": "running", "failure": None}
            run.state["stages"].append(stage)
            try:
                if args.stage == "PREPARE": run.prepare(activate=False)
                elif args.stage not in ("EXPORT", "FINISH"): getattr(run, args.stage.lower())()
                stage["state"] = "completed"
            except (Refused, protection.Refused, OSError, ValueError, KeyError, TypeError, MemoryError, KeyboardInterrupt) as error:
                stage["state"] = "cancelled" if isinstance(error, KeyboardInterrupt) else "failed"
                if isinstance(error, KeyboardInterrupt): error = Refused("operator_interrupted_partial_evidence")
                stage["failure"] = safe_failure(error, run.config, args.stage)["reason"]
                if not run.state.get("primary_failure"): run.fail(error, args.stage)
                if run.state["preparation"]["state"] == "not_performed": run.state["preparation"]["state"] = "failed"
            if args.stage in ("RUN", "FINISH", "EXPORT", "PREPARE"):
                run.finish(args)
            stage["finished_utc"] = utc()
            run.save()
            if args.stage in ("RUN", "FINISH", "PREPARE") or (args.stage == "START" and stage["state"] != "completed"):
                run.export("start-failure" if args.stage == "START" else None)
            elif args.stage == "EXPORT": run.export("recovered-" + uuid.uuid4().hex[:8])
            print("Evidence directory: " + str(run.directory))
            return 0 if stage["state"] == "completed" and not run.state.get("primary_failure") else 1
    except (Refused, protection.Refused, OSError, ValueError, KeyError, TypeError, MemoryError) as error:
        if run:
            run.fail(error, "cleanup_or_export")
            # A failed export never replaces the first preparation/capture failure.
            try: run.export("partial-" + uuid.uuid4().hex[:8])
            except (OSError, ValueError, MemoryError) as secondary: print("Secondary export error: " + type(secondary).__name__)
        else:
            print("Run unavailable: " + (str(error) if isinstance(error, (Refused, protection.Refused)) else type(error).__name__), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
