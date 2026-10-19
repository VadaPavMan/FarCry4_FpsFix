# Far Cry 4 FPS Fix — V2.0

**Far Cry 4 FPS Fix** is a community-made Windows optimization utility designed to improve the game's performance and make applying selected optimization settings more reliable.

V2.0 combines the earlier V1 process-optimization approach with configuration-handling and launch-flow improvements developed during V2.

> **Note:** Performance varies depending on hardware, drivers, graphics settings, and gameplay conditions. No fixed FPS increase is guaranteed.

## Features

- **Process priority management** — Requests the configured Windows priority for Far Cry 4.
- **CPU affinity profiles** — Applies the configured CPU-affinity profile to the game process.
- **Graphics configuration preparation** — Updates supported settings in `GamerProfile.xml` and `GFXSettings.FarCry464.xml`.
- **XML encoding support** — Handles supported UTF-8 and UTF-16 XML files, including UTF-16LE files without a BOM.
- **Configuration backups** — Backs up supported XML files before modifying them.
- **Launch integration** — Includes launcher scripts for supported Steam and Ubisoft Connect workflows.
- **Logging** — Records optimization and configuration operations to help troubleshoot problems.

## Compatibility

- Windows 10/11, 64-bit.
- Far Cry 4 for Windows.
- Steam or Ubisoft Connect, depending on the launcher used.

## Installation

Download the precompiled Windows release from the project's GitHub Releases page. Extract the package into the Far Cry 4 game directory, then follow the platform-specific instructions in [`install.md`](install.md).

You do **not** need to compile the source code to use the precompiled release.

## Performance and limitations

CPU affinity and process priority are not guaranteed to improve performance on every system. Test the same gameplay segment with and without the utility before deciding whether to keep it enabled.

The standard V2.0 configuration does not require DXVK or a custom DLL.

## Documentation

- [`install.md`](install.md) — Installation, launch setup, troubleshooting, and removal.
- [`changelog.md`](changelog.md) — Changes introduced in V2.0 compared with V2.

## Disclaimer

This is an unofficial community project and is not affiliated with or endorsed by Ubisoft. Use it at your own risk. Back up your configuration files before using the utility.
