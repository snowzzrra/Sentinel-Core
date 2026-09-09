# Sentinel Core

An independent native integration layer for **DOOM Eternal**, developed for
[Doom Eternal Archipelago](https://github.com/snowzzrra/DoomEternal-AP-Mod).
Sentinel supplies small native capabilities where authored DECL content and
existing command/RPC mechanisms are insufficient.

[1. Overview](#1-overview) · [2. Current capabilities](#2-current-capabilities) ·
[3. Build and use](#3-build-and-use) · [4. How it works](#4-how-it-works) ·
[5. Roadmap](#5-roadmap) · [6. Contributing](#6-contributing) ·
[7. Credits](#7-credits) · [8. License](#8-license)

## 1. Overview

Sentinel Core gives external tools a versioned boundary to native functionality
inside the game process. Today that boundary supports factual inspection of the
Core itself. Future operations will require their own validation and lifecycle work.

It complements the companion project's authored content and existing tooling.
It is not an Archipelago server/client or a standalone randomizer, and is not
currently a mandatory replacement for Meathook. Archipelago networking, room
progression and player policy remain outside the game.

## 2. Current capabilities

- An `msimg32.dll` bootstrap that forwards the five real System32 exports and
  initializes `sentinel_core.dll` outside loader-lock work.
- C ABI 1 for status inspection and explicit, serialized initialization/shutdown,
  with version/source identity and bounded debugger diagnostics.
- A local, typed Windows named-pipe query of the already loaded Core instance,
  restricted to the same logon session and rejecting remote connections.
- `sentinel_probe.exe`: a standalone read-only PID-targeted CLI with readable and
  JSON output, OS-verified server identity and bounded request deadlines.

Core readiness and IPC availability are separate facts. Engine integration is
**unavailable**; gameplay safety and game-build compatibility are **unprobed**.
Inspection does not access engine layouts, inventory, saves or AP queues.

## 3. Build and use

Use Windows x64, CMake **3.24+**, Visual Studio 2022 / Build Tools with **MSVC v143**
and a Windows SDK. The validated environment uses CMake 3.31.6, MSVC 14.44.35207
and SDK 10.0.26100.0. C++17 and the static MSVC runtime are configured by CMake.
Run these commands from this repository's root in Developer PowerShell, with
CMake and CTest on PATH:

```powershell
cmake -S . -B build/windows -G 'Visual Studio 17 2022' -A x64 -T host=x64
cmake --build build/windows --config Release -- /verbosity:minimal
ctest --test-dir build/windows -C Release --output-on-failure
```

The supported incremental path is the Visual Studio generator. The Release
outputs are in **`build/windows/bin/Release`**:

| File | Purpose |
| --- | --- |
| `msimg32.dll` | Game-side bootstrap/proxy; used together with Core |
| `sentinel_core.dll` | Native Core and inspection service |
| `sentinel_probe.exe` | External query tool; may run from the build directory |
| `sentinel_harness.exe`, `sentinel_import_host.exe`, `sentinel_inspection_tests.exe` | Automated test hosts, not game runtime evidence |

**Current forwarding limitation:** the proxy embeds the build machine's absolute
System32 `msimg32` path. CMake rejects SystemRoot paths containing spaces or dots.
This is a local Windows candidate, not a portable release loader. Build for the
intended environment; do not copy system DLLs or bundle proprietary game files.
Proton and broader platform compatibility remain deferred.

After installing the matching proxy/Core pair with the game stopped, launch the
game normally. The probe neither launches nor injects into it. Resolve its current
PID, requiring a single match:

```powershell
$gameProcesses = @(Get-Process -Name DOOMEternalx64vk -ErrorAction SilentlyContinue)
if ($gameProcesses.Count -ne 1) { throw 'Select exactly one running game process.' }
$doomPid = $gameProcesses[0].Id
.\build\windows\bin\Release\sentinel_probe.exe --pid $doomPid --timeout-ms 2000
.\build\windows\bin\Release\sentinel_probe.exe --pid $doomPid --timeout-ms 2000 --json
```

`--pid` is required; `--timeout-ms` accepts 50–10000 (default 2000); `--json` emits
one JSON object; `--help` shows usage. No launcher, Python environment,
`ap_config.json`, AP connection, generated seed or Meathook RPC is required.

| Exit code | Meaning |
| --- | --- |
| 0 | Valid inspection response; inspect its factual states |
| 2 | Invalid command line |
| 3 | Endpoint absent / target already exited |
| 4 | Access denied |
| 5 | Connection or response timeout |
| 6 | OS server PID or process-instance mismatch |
| 7 | Incompatible wire protocol |
| 8 | Required inspection capability unavailable |
| 9 | Invalid/truncated response |
| 10 | Other I/O failure, including a peer disconnect during exchange |

The CLI requests only the implemented inspection capability. Code 8 is available
through the reusable client API when requesting unsupported capability bits.
See the [wire contract](docs/inspection-protocol.md) for encoding, limits, lifecycle
and error behavior, and the [manual smoke procedure](docs/windows-smoke.md) for
pair replacement, fresh-instance checks and rollback.

## 4. How it works

The proxy's finite initialization worker calls Core after DLL initialization.
Core owns one status snapshot and its explicit lifecycle. Its inspection worker
serves one named-pipe connection at a time; both the C ABI and wire response read
the same snapshot. The wire protocol and C ABI are independent contracts.

The probe connects to `\\.\pipe\sentinel_core.inspect.v1.<PID>` and verifies the
server PID using Windows. It holds a target process handle and matches its OS
creation time to the reply. A new random instance ID distinguishes Core restarts
within a process. Each invocation reads a fresh reply; no DLL is loaded into the
probe and no result cache is used. These checks identify a local instance; they
are not an anti-tamper or cryptographic trust system.

DECL-first remains the design approach for authored content. A future **Game
Link** layer will choose capabilities for external tooling and managed installation.
Meathook may retain its proven RPC role while Sentinel supplies independently
validated native capabilities. Hybrid integration remains evidence-led; future
hooks, full teardown and platform behavior need separate validation.

## 5. Roadmap

The following integrations are **planned**, not currently available:

- Typed native observations, validated game-thread operations and AP Save integration.
- An AP-owned Weapon Upgrade Point economy and native Mission Select/unified campaign.
- Rune registration/presentation, Crucible/Hammer switching and native DeathLink/player telemetry.
- Native HUD, challenge progress and ITEMS FOUND projection.
- Launcher-managed installation/updates and broader compatibility work.

## 6. Contributing

Fixes, research, documentation, ports and improvements are welcome. Keep changes
focused and provide evidence appropriate to the boundary changed: protocol and
lifecycle changes need separate-process tests; game integration needs clearly
identified runtime evidence. Keep generated builds and personal runtime logs out
of source contributions. Describe your compiler, platform and reproduction steps.

You may modify, fork and reuse original project code under its MIT license.
The integration scope guides upstream maintenance; it does not restrict forks.
The [public C header](include/sentinel_core.h), [client interface](include/sentinel_inspection.h)
and [wire documentation](docs/inspection-protocol.md) describe the current narrow
boundary for adaptation.

## 7. Credits

- **SteamKaibz / Kaibz Advanced Options Mod** —
  [DE_AdvancedOptionsModPublic](https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic).
  Independent-DLL, native HUD/control research and coexistence precedent demonstrated
  that this class of integration is possible.
- **chrispy / Meathook** — [Meathook](https://github.com/brongo/m3337ho0o0ok).
  Its native hooking/RPC foundation, technical ideas and existing integration
  informed Sentinel's independent design.
- **snowzzrra / Doom Eternal Archipelago** —
  [companion project](https://github.com/snowzzrra/DoomEternal-AP-Mod).
  Provides the integration requirements, DECL-first design and external AP
  client/content context that motivated Sentinel Core.

Meathook and Kaibz are technical references: no code or assets from either project
are incorporated here, and neither is a runtime dependency of Core or the probe.

## 8. License

Original Sentinel Core code is available under the [MIT License](LICENSE).
