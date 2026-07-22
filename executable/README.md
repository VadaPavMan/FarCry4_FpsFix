# Far Cry 4 FPS Fix

A native Windows launcher for Far Cry 4 that improves performance and frame pacing on many modern multi-core CPUs. It runs beside the game: it never launches or modifies `FarCry4.exe` itself.

## Quick start

1. Extract the `FC4-FpsFix` folder anywhere convenient. Keeping it in the `Far Cry 4 install folder` is recommended.
2. Double-click `FarCry4_FPSFix.exe` **before** starting the game.
3. Leave its window open.
4. Start Far Cry 4 normally from Steam, Ubisoft Connect, or Epic Games.
5. After the game closes, the optimizer detects that it has exited and ends its session.

This manual method is the supported launch method. Do not use custom launcher command lines or replace the game executable. Steam, Ubisoft Connect, or Epic Games remains responsible for launching the game, which keeps its normal achievement and overlay flow intact.

See [INSTALL.md](INSTALL.md) for the complete guide.

## What it does

When started, the EXE first checks the Far Cry 4 profile directory under `Documents\My Games\Far Cry 4` and applies these performance settings when needed:

- In `GamerProfile.xml`, it sets `DisableLoadingMip0="1"` and `GPUMaxBufferedFrames="1"`.
- In `GFXSettings.FarCry464.xml`, it sets the `GEOMETRY` option to `Value="high"`.
- It creates a one-time `.fc4fpsfix.backup` copy before changing either XML file.

It then waits for `farcry4.exe`, waits the configured initialization period, and applies:

- High process priority.
- A four-logical-processor affinity profile: Intel `0, 2, 4, 6`; AMD `2, 4, 6, 8`.
- Optional 1 ms timer resolution while the game runs.
- Optional memory working-set trimming and High Performance power-plan switching.

The XML changes trade some image quality for smoother performance: disabling the highest mip level can reduce texture sharpness, and Geometry is intentionally capped at High. The CPU/process changes apply only while Far Cry 4 is running; the timer resolution and optional power plan are restored after the game exits.

## Configuration

Edit `config/settings.json` next to the EXE:

| Setting | Default | Purpose |
|---|---:|---|
| `ProcessName` | `farcry4` | Process to detect; normally leave unchanged. |
| `Priority` | `High` | Game process priority. Do not use `RealTime`. |
| `UseSmartAffinity` | `true` | Uses the proven four-core affinity profile. |
| `WaitSecondsAfterLaunch` | `30` | Time to wait after detection before applying process settings. |
| `EnableTimerResolution` | `true` | Uses 1 ms timer resolution while playing. |
| `ChangePowerPlan` | `false` | Temporarily enables High Performance power plan. |
| `EnableMemoryCleanup` | `false` | Periodically trims the game working set. |
| `MemoryCleanupIntervalSeconds` | `120` | Interval for optional memory cleanup. |
| `LogToFile` | `false` | Writes `optimizer.log` beside the EXE for troubleshooting. |

## Restore XML settings

To restore the profile files from the backups created by this tool, open Command Prompt in the `FC4-FpsFix` folder and run:

```
FarCry4_FPSFix.exe --restore-xml-settings
```

Run this while Far Cry 4 is closed. Starting the optimizer normally afterward applies the performance XML values again.

## Open source

The native C++ source is included in `source/FarCry4_FPSFix.cpp` so users can inspect, audit, and build the launcher themselves. It uses only Windows APIs and the C++ standard library—no PowerShell, batch files, injection, or game-binary patching.

Example MinGW-w64 build command:

```
g++ -std=c++20 -O2 -s -static -static-libgcc -static-libstdc++ -municode source\FarCry4_FPSFix.cpp -o FarCry4_FPSFix.exe -lwinmm -lshell32 -lole32 -ladvapi32 -luuid
```

## Notes

- Results vary by hardware, resolution, drivers, and in-game settings; this is not a guaranteed FPS target.
- For additional GPU-limited performance, lower expensive in-game options such as GameWorks effects, Soft Shadows, TXAA, or Geometry.
- This is an unofficial fan-made tool and is not affiliated with Ubisoft, Valve, or Epic Games.

## Credits

The CPU-affinity approach and XML tuning are based on community troubleshooting documented by [PCGamingWiki's Far Cry 4 page](https://www.pcgamingwiki.com/wiki/Far_Cry_4).
