# Installation & Setup Guide

This guide covers all three ways to use **FarCry4-FpsFixMod**.
Pick the method that matches how you launch Far Cry 4.

---

## Step 1 -- Copy the Mod Files

1. Download and extract the mod zip file.
2. Open the extracted folder. You will see:
   ```
   config/
   launcher/
   SteamLauncher.bat
   UbisoftLauncher.bat
   gameProcess.bat
   ```
3. Select all of these files and folders.
4. Navigate to your **Far Cry 4 install folder**. The default locations are:
   - Steam: `C:\Program Files (x86)\Steam\steamapps\common\Far Cry 4\`
   - Ubisoft Connect: `C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\games\Far Cry 4\`
5. Paste everything directly into the Far Cry 4 folder.

Your game folder should now look like this:

```
Far Cry 4/
|-- bin/
|   |-- FarCry4.exe
|-- config/                 <- added by mod
|   |-- settings.json
|-- launcher/               <- added by mod
|   |-- process.ps1
|-- SteamLauncher.bat       <- added by mod
|-- UbisoftLauncher.bat     <- added by mod
|-- gameProcess.bat         <- added by mod
|-- data_win32/
|-- ...
```

---

## Step 2 -- Choose Your Launch Method

---

### Method A -- Steam

This method runs the optimizer automatically every time you click Play in Steam.

1. Open **Steam**.
2. Go to your **Library** and right-click **Far Cry 4**.
3. Click **Properties**.
4. Click **General** in the left sidebar.
5. Find the **Launch Options** field.
6. Paste the following into it exactly as shown:

```
"SteamLauncher.bat" && %command%
```

7. Close the Properties window.

That is all. From now on, every time you click Play in Steam:
- The optimizer starts silently in the background.
- Steam launches Far Cry 4 normally through its own process.
- The optimizer detects the running game and applies all settings automatically.
- When you quit the game, all changes are reversed.

> **Note:** A small black window may flash for a moment when you click Play. This is normal -- it is the optimizer starting up.

---

### Method B -- Ubisoft Connect (One-Click)

This method uses `UbisoftLauncher.bat` as your shortcut to launch the game.
You use this instead of clicking Play in Ubisoft Connect.

1. Navigate to your Far Cry 4 install folder.
2. Right-click **UbisoftLauncher.bat** and click **Send to > Desktop (create shortcut)**.
3. Rename the shortcut on your desktop to `Far Cry 4` or whatever you prefer.
4. From now on, double-click this shortcut to launch the game.

What happens when you run it:
- The optimizer starts silently in the background.
- Ubisoft Connect opens and launches Far Cry 4 normally through its own process.
- The optimizer detects the running game and applies all settings automatically.
- When you quit the game, all changes are reversed.

> **Achievements work** because the game is still launched through Ubisoft Connect's own process chain -- the optimizer never intercepts the launch.

---

### Method C -- Manual

This method works with both Steam and Ubisoft Connect. Use it if you prefer not to change any launch settings.

1. Navigate to your Far Cry 4 install folder.
2. Double-click **gameProcess.bat**. A window will open and show:
   ```
   FarCry4-FpsFixMod is now running in this window.
   Launch Far Cry 4 normally through Steam or Ubisoft Connect.
   ```
3. Launch Far Cry 4 normally through Steam or Ubisoft Connect as you usually would.
4. The optimizer will detect the game and apply all settings automatically.
5. Keep the gameProcess.bat window open while you play. It will close on its own when you quit the game.

> This method requires you to remember to run `gameProcess.bat` before each play session.

---

## Step 3 -- Verify It Is Working

After launching the game, open the file `optimizer.log` in your Far Cry 4 install folder.
It should contain lines like this:

```
[00:00:01] === FarCry4-FpsFixMod Started ===
[00:00:01] Timer resolution: set to 1ms (default is 15.6ms). Reduces hitches.
[00:00:03] Waiting for farcry4 to start...
[00:00:45] Game window appeared after 42s. Applying optimizations...
[00:00:48] Process priority: set to High
[00:00:48] CPU affinity: mask=340 (physical cores 1-4, no HT, no core 0)
[00:00:48] Thread priority: boosted 18 threads to Highest
[00:00:48] Fullscreen optimizations disabled. Lower frame latency.
[00:00:48] GPU processes boosted: nvcontainer, nvidia share
[00:00:48] --- All optimizations applied ---
```

If you see those lines, the mod is working correctly.

---

## Uninstall

**To remove the mod completely:**

1. If you used Method A (Steam), open Steam -> Far Cry 4 -> Properties -> General and clear the Launch Options field.
2. Delete the following from your Far Cry 4 install folder:
   - `config/` folder
   - `launcher/` folder
   - `SteamLauncher.bat`
   - `UbisoftLauncher.bat`
   - `gameProcess.bat`
   - `optimizer.log` (if present)

The mod does not write to the registry permanently (all registry changes are reversed when the game exits) and does not modify any game files.

---

## Troubleshooting

**The game does not start when I click Play in Steam.**
Make sure the Launch Options field contains exactly:
```
"SteamLauncher.bat" && %command%
```
Check that `SteamLauncher.bat` is in the root of your Far Cry 4 folder (not in a subfolder).

**optimizer.log shows the game was never detected.**
The optimizer waits up to 5 minutes for the game process to appear. If it still does not detect it, check that the `ProcessName` in `config/settings.json` is set to `farcry4` (lowercase, no .exe).

**I see a PowerShell error about execution policy.**
Right-click `gameProcess.bat` and select Run as Administrator for the first launch. Alternatively, open PowerShell as Administrator and run:
```
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

**The mod is running but I do not notice a difference.**
Make sure to check `optimizer.log` to confirm settings were actually applied. If `ChangePowerPlan` is set to false in `settings.json`, try enabling it (set to `true`) for additional performance. Also consider running `gameProcess.bat` as Administrator to allow power plan changes.