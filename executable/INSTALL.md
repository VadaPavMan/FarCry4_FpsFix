# Installation and Use

## Requirements

- Windows 10 or Windows 11 (64-bit)
- Far Cry 4 installed through Steam, Ubisoft Connect, or Epic Games
- Permission to edit your own Far Cry 4 profile files in `Documents\My Games\Far Cry 4`

No PowerShell, batch file, or separate runtime is required.

## Install

1. Extract the release folder named `FC4-FpsFix`.
2. Copy the below mentioned files & folder and paste it in Far Cry 4 installation folder:
   ```
       |-- FarCry4_FPSFix.exe
       |-- config/
       |-- source/
   ```
   The folder can technically be stored elsewhere, but `FarCry4_FPSFix.exe` and its `config` folder must stay together.

## Play Far Cry 4

Use this same method regardless of whether you own the game on Steam, Ubisoft Connect, or Epic Games:

1. Open Far Cry 4 installation folder.
2. Double-click `FarCry4_FPSFix.exe` to run the mod, if you want you can make the Desktop Shortcut of this .exe which will be easier to click and run.
3. (Optional) To Make Desktop Shortcut: Right Click on

```
`FarCry4_FPSFix.exe` -> Show More Options -> Send to -> Desktop (create shortcut).
```

4. The optimizer window reports that it is waiting for `farcry4.exe`. Leave this window open.
5. `Launch Far Cry 4` normally using the Play button in Steam, Ubisoft Connect, or Epic Games.
6. The optimizer detects the game, waits 30 seconds by default, applies its process settings, and monitors the session.
7. Quit Far Cry 4 normally. The optimizer restores temporary timer/power-plan settings and then exits.

Do not set Steam, Ubisoft Connect, or Epic Games launch options for this mod. The game must continue to be launched by its normal store launcher. That is the supported approach for normal achievements, cloud saves, overlay behavior, and entitlement checks.
I Tried to test with the launch option the game is not even launching. so you have to follow the above steps every time you play the game.

## Automatic profile changes

Before waiting for the game, the EXE searches these generated profile files:

```
Documents\My Games\Far Cry 4\<your-profile>\GamerProfile.xml
Documents\My Games\Far Cry 4\<your-profile>\GFXSettings.FarCry464.xml
```

It only changes a value when necessary:

- `DisableLoadingMip0="1"`
- `GPUMaxBufferedFrames="1"`
- `GEOMETRY` is set to `Value="high"`

The tool creates a `.fc4fpsfix.backup` file before its first change to an XML file. If Far Cry 4 has not been run on the PC yet, the XML files may not exist; launch the game once normally, close it, and then start the FPS fix again.

## Verify it is working

The optimizer window should show messages similar to:

```
XML performance settings checked and updated.
Waiting for farcry4.exe to launch...
Game detected. Waiting 30s for full initialization...
Priority set to: High
Affinity set. Mask=85 | Using Intel core profile.
All optimizations applied. Monitoring until game exits...
```

For a persistent log, set `"LogToFile": true` in `config\settings.json`. The log will be written as `optimizer.log` beside the EXE.

## Restore the XML files

If you want to restore the original XML files, close Far Cry 4, open Command Prompt in the `FC4-FpsFix` folder, and run:

```
FarCry4_FPSFix.exe --restore-xml-settings
```

This restores only files that have a `.fc4fpsfix.backup` created by this tool. A later normal run of the FPS fix applies the performance settings again.

## Troubleshooting

**The optimizer says it cannot find the profile files.** Start Far Cry 4 once normally to create its Documents profile, close the game, then run the EXE again.

**The optimizer never detects the game.** Confirm `"ProcessName": "farcry4"` in `config\settings.json`, then start the game normally from its launcher within five minutes of starting the optimizer.

**The game looks less sharp.** This is expected from `DisableLoadingMip0="1"`, which reduces texture quality to help stutter. Restore the XML backup if you prefer the original image quality.

**Windows blocks the EXE.** Verify that you downloaded the release from the mod's trusted source. The matching C++ source is included in `source\FarCry4_FPSFix.cpp` for inspection.
