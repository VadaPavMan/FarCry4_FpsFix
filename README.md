# FarCry4-FpsFixMod

A lightweight performance optimizer for **Far Cry 4** on modern PCs.
Far Cry 4 was built for 4-core CPUs in 2014 and does not scale well on modern hardware --
this mod fixes that by tuning how Windows schedules the game, without touching any game files.

> **Safe for achievements** on both Steam and Ubisoft Connect.

---

## Before:
[![Watch the video](screenshots/before.png)](https://youtu.be/vZfe1422wrM)

## After:
[![Watch the video](screenshots/after.png)](https://youtu.be/5c7NeJk0m_I)


---

## What It Does

| Optimization | What it fixes |
|---|---|
| **CPU Affinity** | Pins the game to physical cores 1-4 only (no hyperthreading siblings, no core 0). Stops Windows from migrating game threads to slow efficiency cores or interrupt-heavy core 0. |
| **High Process Priority** | Gives Far Cry 4 more CPU time over background applications. |
| **Thread Priority Boost** | Bumps every thread inside the game process to Highest, tightening frame timing on the render and physics threads. |
| **1ms Timer Resolution** | Windows default system timer fires every 15.6ms. At this rate the game loop wakes up late regularly, causing hitches. Dropping to 1ms makes the scheduler far more responsive. |
| **GPU Process Boost** | Finds NVIDIA / AMD / Intel GPU helper processes and boosts their priority to reduce stalls during texture streaming. |
| **Disable Fullscreen Optimizations** | Windows wraps fullscreen DX11 games in a borderless overlay that adds latency. This disables it for lower frame latency. Restored on exit. |
| **Memory Cleanup** *(optional)* | Periodically trims the game's working set to reduce RAM fragmentation. Useful on 8GB systems. Off by default. |
| **Auto Restore** | Every change is reversed when you quit the game. Nothing is left behind. |

---

## Requirements

- Windows 10 or Windows 11
- PowerShell 5.1 or later (included in Windows by default)
- Far Cry 4 (Steam or Ubisoft Connect)

---

## Installation

See **[INSTALL.md](INSTALL.md)** for full setup instructions for Steam, Ubisoft Connect, and the manual method.

---

## File Structure

```
Far Cry 4/                      <- drop everything here
|-- config/
|   |-- settings.json           <- tweak options here
|-- launcher/
|   |-- process.ps1             <- main optimizer script
|-- SteamLauncher.bat           <- used for Steam launch option
|-- UbisoftLauncher.bat         <- used for Ubisoft Connect (one-click)
|-- gameProcess.bat             <- manual launch method
```

---

## Configuration

Open `config/settings.json` to adjust behavior:

```json
{
    "ProcessName":                  "farcry4",
    "Priority":                     "High",
    "UseSmartAffinity":             true,
    "BoostThreadPriority":          true,
    "BoostGpuProcesses":            true,
    "DisableFullscreenOptimize":    true,
    "EnableTimerResolution":        true,
    "ChangePowerPlan":              false,
    "EnableMemoryCleanup":          false,
    "MemoryCleanupIntervalSeconds": 120,
    "LogToFile":                    true
}
```

| Setting | Default | Description |
|---|---|---|
| `Priority` | `High` | Process priority. Use `High`. Never use `RealTime` -- it can freeze your PC. |
| `UseSmartAffinity` | `true` | Pin game to physical cores 1-4, skip core 0 and HT siblings. |
| `BoostThreadPriority` | `true` | Boost all game threads to Highest within the High priority class. |
| `BoostGpuProcesses` | `true` | Boost NVIDIA/AMD/Intel GPU helper process priorities. |
| `DisableFullscreenOptimize` | `true` | Disable Windows fullscreen optimizations for lower frame latency. |
| `EnableTimerResolution` | `true` | Set system timer to 1ms for smoother frame pacing. |
| `ChangePowerPlan` | `false` | Switch to High Performance power plan while the game runs. Enable if your CPU clocks down during gameplay. Requires admin. |
| `EnableMemoryCleanup` | `false` | Trim RAM periodically. Recommended on 8GB systems. |
| `MemoryCleanupIntervalSeconds` | `120` | How often to trim memory (seconds). |
| `LogToFile` | `true` | Write `optimizer.log` in the game folder so you can verify what ran. |

---

## How It Works

The mod does **not** launch the game itself. Steam or Ubisoft Connect always launch the game normally through their own process chain -- that is what keeps achievements working. The optimizer starts in the background, waits until it detects the running `farcry4.exe` process, then attaches to it and applies the settings from outside. When you quit the game, every change is reversed.

---

## FAQ

**Will this break my achievements?**
No. The game is always launched through Steam or Ubisoft Connect normally. The optimizer only adjusts Windows process settings from outside -- equivalent to manually changing priority in Task Manager.

**Does it work with mods, ENB, or ReShade?**
Yes. This mod only touches process scheduling, not game files. It is fully compatible with any other mod.

**My CPU has more than 4 cores -- does affinity hurt performance?**
No. Far Cry 4's Dunia Engine 2 was designed for 4 physical cores and does not scale past that. Giving it more cores causes more inter-core synchronization overhead, not more performance. Pinning to 4 fast cores gives it what it was designed for.

**Can I use this on other Ubisoft games?**
The `ProcessName` in `settings.json` can be changed to any process name. The other optimizations apply generically. Results will vary by game engine.

