# Installation Guide — Far Cry 4 FPS Fix V2.0

This guide covers the precompiled Windows release. **You do not need Visual Studio, CMake, or a C++ compiler.**

## Requirements

- A Windows 10/11 64-bit PC.
- A Windows installation of Far Cry 4.
- The precompiled FPS Fix release downloaded from the project's official GitHub Releases page.

## Step 1 — Download and extract

1. Download the V2.0 Windows ZIP from GitHub Releases.
2. Extract the archive.
3. Locate your Far Cry 4 game directory—the folder containing `farcry4.exe`.
4. Copy the package files into the game directory, preserving the folder structure supplied in the archive.

Do not rename, replace, or delete the original game executable.

## Step 2 — Choose your launcher

Use the launcher matching your platform:

# For Steam Version:

- **Steam:** `SteamLauncher.bat`
- After Putting The Files & Folder To Game's Directory Look For `SteamLauncher.bat` And Right Click On It Then Copy Path.
- Open Steam Then Library And Locate The Far Cry 4 Then Right Click On It And Properties After That Look For `Luanch Options` After That Paste The Path You Copied For `SteamLauncher.bat` Example: `"D:\Program Files\steamapps\common\Far Cry 4\FarCry4_FPSFix_V2\SteamLauncher.bat" && %command%`
- Note: Make Sure After Pasting The File Path Add This Line -> && %command%
- Done Now Enjoy.

# For Ubisoft Connect:

- **Ubisoft Connect:** `UbisoftLauncher.bat`
- You Use It Like Other Platforms Method, Mentioned Below
- Or You Can Do The Steam Like Method Too For This. (I Tried, But Didn't Worked For Me)

# For Other Platforms:

- **Other Platforms:** Use `gameProcess.bat`.
- How to use ?: Before Running The Game Just Double Click (Run) On `gameProcess.bat`
- Keep It Running In Background.
- Close When You Exit The Game.
- Done Enjoy.

Follow the platform-specific launch configuration included with the release. Merely copying a batch file into the game folder does not automatically configure Steam or Ubisoft Connect to execute it.

Configure the intended launch method once. Avoid starting multiple optimizer workers for the same game session.

## Step 3 — Launch the game

1. Close Far Cry 4 if it is already running.
2. Start the game using the configured launch method.
3. Wait for the game to finish starting.
4. Check `optimizer.log` if logging is enabled.
5. Verify that the log reports successful process detection and the results of the priority and affinity operations.

A successful launch does not necessarily mean performance has improved. Compare the same save, location, resolution, graphics settings, and gameplay route with and without the utility.

## Troubleshooting

### The game process is not detected

- Close the game and retry.
- Confirm that you used the configured launcher.
- Check the process name in the configuration.
- Review `optimizer.log` for errors.

### The game launches, but optimization settings do not apply

- Check the log for failed priority, affinity, or XML operations.
- Verify that the executable and required configuration files are in the expected locations.
- Make sure that only one optimizer worker is running.

### Performance becomes worse or unstable

- Close the game and disable the utility.
- Compare against the unmodified game.
- Use the documented default affinity configuration before experimenting with other profiles.
- Remember that higher process priority and restricted CPU affinity can sometimes reduce performance.

### Restore the original XML settings

1. Close Far Cry 4.
2. Use the XML restoration option or procedure supplied with the release.
3. Confirm that the original files were restored.
4. Keep the backups until you have verified the result.

## Uninstall

1. Close Far Cry 4 and stop the optimizer worker.
2. Restore backed-up XML files using the documented restoration procedure.
3. Remove the FPS Fix files added to the game directory.
4. Keep `optimizer.log` if you intend to report an issue.

Do not remove or modify original game files during uninstallation.

## Important

Performance improvements depend on your system. No particular FPS increase is guaranteed. If security software flags the executable, verify that it came from the official release and check the published checksum rather than blindly disabling protection.
