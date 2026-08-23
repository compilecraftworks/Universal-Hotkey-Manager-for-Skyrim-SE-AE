# MCM integration

UHI_MCM.psc is the SkyUI MCM front end. The intended in-game path is:

ESC → Mod Configuration → Universal Hotkey Manager for Skyrim SE-AE → 전체 스캔

The SKSE DLL listens for UHI_StartScan, runs the four-stage pipeline off the game loading path, and broadcasts UHI_ScanProgress with the active stage name and 0–100 progress. Compile this source with the SkyUI script sources before packaging the .pex file.
