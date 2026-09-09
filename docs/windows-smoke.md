# Windows live-query smoke

Phase 3.2 is implemented and harness-tested; **fresh DOOM runtime evidence is
pending**. The maintainer performs this smoke. The Phase 3.1 bootstrap/coexistence
PASS applies to the earlier pair, not automatically to these changed binaries.

Run the commands from this repository's root. Use the normal Windows logon
running the game; the probe does not require elevation. The pair is a local
Windows candidate with the [System32 forwarding limitation](../README.md#3-build-and-use).

## 1. Stop and preserve the tested pair

Quit DOOM normally and ensure its process is gone before copying anything.
Set the actual installation path. The backup directory is outside the game and
inside the ignored build tree; retain it through the smoke.

```powershell
$gameRoot = Read-Host 'Full path to the DOOMEternal installation'
if (Get-Process -Name DOOMEternalx64vk -ErrorAction SilentlyContinue) { throw 'Stop DOOM first.' }
$release = Join-Path $PWD 'build\windows\bin\Release'
$pair = @('msimg32.dll', 'sentinel_core.dll')
$backup = Join-Path $PWD ('build\smoke-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -ErrorAction Stop | Out-Null
foreach ($name in $pair) {
    Copy-Item -LiteralPath (Join-Path $gameRoot $name) -Destination $backup -ErrorAction Stop
}
Get-FileHash -Algorithm SHA256 -LiteralPath ($pair | ForEach-Object { Join-Path $backup $_ })
foreach ($name in $pair) {
    Copy-Item -LiteralPath (Join-Path $release $name) -Destination (Join-Path $gameRoot $name) -ErrorAction Stop
}
Get-FileHash -Algorithm SHA256 -LiteralPath ($pair | ForEach-Object { Join-Path $gameRoot $_ })
```

This procedure replaces an existing tested pair; a missing backup source is an
error. Keep the installed Meathook/XINPUT configuration unchanged. Copy neither
system DLLs nor harness binaries into the game. The probe can run from `$release`.
If copying either new file fails, restore **both** backed-up files before launch.

## 2. Launch normally and inspect the current process

Launch DOOM yourself through the normal setup. No launcher/AP client or RPC
activation is needed for this query.

```powershell
$gameProcesses = @(Get-Process -Name DOOMEternalx64vk -ErrorAction SilentlyContinue)
if ($gameProcesses.Count -ne 1) { throw 'Select exactly one running game process.' }
$doomPid = $gameProcesses[0].Id
& "$release\sentinel_probe.exe" --pid $doomPid --timeout-ms 2000
$firstText = & "$release\sentinel_probe.exe" --pid $doomPid --timeout-ms 2000 --json
if ($LASTEXITCODE -ne 0) { throw $firstText }
$first = $firstText | ConvertFrom-Json
$firstText
```

Expect exit **0**, version `0.2.0-phase3.2`, wire 1 / ABI 1, requested/OS server
PID agreement, `Core=ready`, `IPC=listening`, core capabilities 3 and inspection
capabilities 1. `engine_integration` is `unavailable`; `gameplay_safety` and
`game_build` are `unprobed`. JSON `host_kind` should be
`doom_executable_name_match`; inspect the OS-derived `host_path` as well. A
`non_game_host` reply proves only that host, never a DOOM PASS. Record the current
`build_id`, `process_created` and `instance_id`, not a historical PID.

## 3. Reconnect, exit and relaunch

```powershell
$againText = & "$release\sentinel_probe.exe" --pid $doomPid --timeout-ms 2000 --json
if ($LASTEXITCODE -ne 0) { throw $againText }
$again = $againText | ConvertFrom-Json
if ($again.instance_id -ne $first.instance_id -or $again.process_created -ne $first.process_created) {
    throw 'Instance changed during reconnect; investigate.'
}
```

Quit normally, confirm the process disappears, then query the **old** PID once:

```powershell
& "$release\sentinel_probe.exe" --pid $doomPid --timeout-ms 2000 --json
```

Expect `endpoint_absent`/exit 3 after exit. If Windows has already reused the PID,
inspect the newly reported outcome and identity; never treat that PID alone as
the original instance. Launch DOOM normally again, rerun the PID discovery above,
and capture a fresh reply:

```powershell
$gameProcesses = @(Get-Process -Name DOOMEternalx64vk -ErrorAction SilentlyContinue)
if ($gameProcesses.Count -ne 1) { throw 'Select exactly one running game process.' }
$doomPid = $gameProcesses[0].Id
$newText = & "$release\sentinel_probe.exe" --pid $doomPid --timeout-ms 2000 --json
if ($LASTEXITCODE -ne 0) { throw $newText }
$fresh = $newText | ConvertFrom-Json
if ($fresh.instance_id -eq $first.instance_id -or $fresh.process_created -eq $first.process_created) {
    throw 'Expected a fresh process and Core instance after relaunch.'
}
$newText
```

Version/build identity should stay the same for the same installed pair; process
creation and instance identity must change. The probe opens the endpoint and
reads anew on every invocation. Record actual outcomes and normal exit/relaunch
behavior; only new evidence can mark this live-query smoke PASS.

## 4. Roll back

Quit DOOM and restore the previous pair together from the saved `$backup` path:

```powershell
if (Get-Process -Name DOOMEternalx64vk -ErrorAction SilentlyContinue) { throw 'Stop DOOM first.' }
foreach ($name in $pair) {
    Copy-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $gameRoot $name) -ErrorAction Stop
}
Get-FileHash -Algorithm SHA256 -LiteralPath ($pair | ForEach-Object { Join-Path $gameRoot $_ })
```

Compare against the captured backup hashes. Restore both files even if only one
changed. This smoke needs no save/configuration edits or launch-option changes.
It does not validate engine hooks, gameplay delivery, hot unload, portable
distribution, Proton or the complete future Hybrid architecture.
