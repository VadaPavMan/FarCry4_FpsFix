# Changelog

## V2.0 — Combined Release

V2.0 combines the V1 process-optimization approach with configuration and launch-flow improvements developed during V2.

### Added and improved

- **Combined optimization logic:** Brought the earlier V1 process-management approach together with the newer V2 configuration and launch-handling work.
- **CPU affinity handling:** Added configurable affinity-profile handling instead of relying solely on fixed logical-processor indexes. Results remain hardware-dependent.
- **Process priority:** Retained configurable game-process priority and improved reporting of whether Windows accepted the requested change.
- **XML configuration preparation:** Added the newer configuration-handling logic for supported `GamerProfile.xml` and `GFXSettings.FarCry464.xml` files.
- **XML encoding handling:** Improved handling of UTF-16LE files without a BOM, alongside supported UTF-8 input.
- **Configuration backups:** Backs up supported XML files before modifying them and provides a restoration procedure.
- **Launch and process detection:** Refined the launch flow and process-detection behavior so runtime settings can be applied after the game process is established.
- **Logging:** Retained and improved diagnostic logging for configuration preparation and process-setting results.

### Notes

- V2.0 does not guarantee a specific FPS increase.
- CPU affinity and process priority can have different effects across hardware configurations.
- DXVK is not required by the standard V2.0 configuration.
- Benchmark results should be collected using the same gameplay segment and settings for a meaningful comparison.

## V2

V2 introduced the newer optimization and launch/configuration-handling work that was subsequently refined and combined with the earlier V1 approach in V2.0. Consult the repository history and V2 release notes for the exact feature set of that release.
