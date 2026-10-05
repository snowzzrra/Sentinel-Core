# Sentinel Core

Sentinel Core is a C++ runtime for DOOM Eternal mod development. It gives native
clients access to game state, gameplay operations and campaign saves through a
local API. [Doom Eternal Archipelago](https://github.com/snowzzrra/DoomEternal-AP-Mod)
uses Core for its integration with the game.

Current source version: **1.0.1**. Core builds for **Windows x64**.

## Features

With Core, a mod can:

- Read the current map, player inventory, weapon upgrades, runes and special weapons.
- Apply item grants, refill ammunition and trigger DeathLink effects.
- Create, read, back up and restore campaign saves, and display campaign entries in the game menus.
- Update Automap and Fast Travel state, and confirm interactions with in-game objects.

Core also provides console access and the `noclip` and `chrispy` commands for
exploring maps and testing entities.

## Using Core

For Doom Eternal Archipelago, use the companion project's launcher to install
and configure the runtime.

The runtime package contains:

| File | Purpose |
| --- | --- |
| `sentinel_core.dll` | Game integration and native API. |
| `msimg32.dll` | Loads Core and forwards graphics calls to Windows. |
| `sentinel_probe.exe` | Reads runtime status and game observations. |
| `distribution.json` | Version, compatibility information and file hashes. |
| `SHA256SUMS.txt` | Checksums for the distribution files. |
| `notices/` | Licenses and third-party notices. |

Keep the Core DLL and bootstrap from the same package. Core checks their build
identity and the game executable before starting its integration.

## Console tools

Use these commands in the game console:

| Command | Action |
| --- | --- |
| `noclip` | Toggles movement through walls. |
| `noclip on` / `noclip off` | Sets noclip movement. |
| `chrispy <entitydef>` | Spawns a loaded entity definition at the point you aim at. |
| `chrispy <entitydef> <x> <y> <z>` | Spawns the entity at the given coordinates. |
| `condump AP_SUPPORT_FILE.txt` | Writes the console output to a support file. |

## Build

Use Windows x64 with the Visual Studio C++ tools, Windows SDK, CMake and Ninja.
CI uses MSVC 14.44.35207 and CMake 3.31.6.

Open an **x64 Native Tools Command Prompt** and run these commands from the
repository root:

```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The DLLs and probe executable are in `build/bin`.

## Development and diagnostics

Start with [sentinel_inspection.h](include/sentinel_inspection.h) for the C++
client API. The headers in `include/` define the gameplay and save interfaces.
Clients communicate with Core through a local named pipe for the game process.

To inspect a running instance, replace `1234` with the game's process ID:

```cmd
build\bin\sentinel_probe.exe --pid 1234 --json
```

Use `sentinel_probe.exe --help` for game-state and save inspection options.
The probe reports runtime version, build identity and request status. For
queued operations, a dispatch result records the engine call; use the relevant
game-state query to check its effect.

## Credits and licenses

Native integration references:

- [Kaibz Advanced Options Mod](https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic) by SteamKaibz. This mod proved some hud things could be done.
- [Meathook](https://github.com/brongo/m3337ho0o0ok) by chrispy, who gave me permission to modify Meathook, so I used it to keep some of the features AP-MOD needed. Every single Meathook-owned file and function will eventually be removed.

- [Doom Eternal Archipelago](https://github.com/snowzzrra/DoomEternal-AP-Mod) by snowzzrra.

Sentinel Core uses the [MIT License](LICENSE). MinHook and HDE have their own
[license](third_party/minhook/LICENSE.txt) and [notice](third_party/minhook/NOTICE).
