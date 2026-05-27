# Architecture

WKOpenVR is a single SteamVR driver DLL + a single overlay binary that compose the release modules under one process tree.

## Layout

```
WKOpenVR/
  core/
    src/
      common/   -- Protocol.h, shared utilities used by both sides
      driver/   -- SteamVR driver entry (driver_wkopenvr.dll); MinHook setup;
                   per-module driver-side activation via FeatureFlags
      overlay/  -- WKOpenVR.exe shell: dashboard overlay rendering, tabs,
                   shared ImGui helpers, the Modules tab that toggles flag files
  modules/
    calibration/   -- src/driver/, src/overlay/, tests/
    smoothing/     -- src/driver/, src/overlay/
    inputhealth/   -- src/driver/, src/overlay/, tests/
  driver_wkopenvr/  -- staging tree (manifest + resources + bin/win64) copied
                       into release zips and the installer
```

`core/` owns process-global state: MinHook installs, the IPC server, the dashboard overlay handle, the umbrella's tab shell. Each `modules/<feature>/` is otherwise independent: its own static lib pair (driver-side + overlay-side), its own pipe name, its own profile file under `%LocalAppDataLow%\WKOpenVR\`.

## Shared driver DLL

A SteamVR driver hooks into `vrserver.exe` via MinHook. MinHook is process-global: only one detour can exist per target function. Four separate driver DLLs trying to patch the same slot of `IVRDriverContext::GetGenericInterface` would collide -- the second install silently fails and that driver's detours never fire. The umbrella driver installs each hook once and dispatches inside the detour by feature, so any subset of features can be enabled simultaneously without conflict.

The driver DLL ships in the release zip as `driver_01wkopenvr.dll`. The `01` prefix forces SteamVR's alphabetic-sort driver loader to evaluate it after any other drivers, which matters for the calibration pose hook.

## Feature flags

Each feature is gated by a marker file in the driver's resources directory:

| Flag file | Module | What activates |
|---|---|---|
| `enable_calibration.flag` | calibration | pose-update hook + `\\.\pipe\WKOpenVR-Calibration` |
| `enable_smoothing.flag` | smoothing | skeletal hook + `\\.\pipe\WKOpenVR-Smoothing` |
| `enable_inputhealth.flag` | inputhealth | boolean/scalar input hooks + `\\.\pipe\WKOpenVR-InputHealth` + 10 Hz snapshot shmem |

The driver scans the resources directory at startup; flag presence is checked once per SteamVR session. The umbrella overlay's Modules tab manages these files through an elevated PowerShell helper -- because the driver tree lives under `Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\01wkopenvr\`, dropping or removing flag files needs an admin token. The toggle takes effect at the next SteamVR launch.

## Process tree at runtime

1. SteamVR loads `driver_01wkopenvr.dll` via the normal driver discovery path.
2. The driver scans `resources/enable_*.flag`, decides which feature modules to activate, and opens the corresponding IPC servers.
3. The user launches the umbrella overlay (`WKOpenVR.exe`). It registers itself as a SteamVR application via the bundled vrmanifest so it can be auto-launched the next time. The overlay connects to each module's IPC pipe, presents per-module tabs, and renders into both the desktop window and the SteamVR dashboard via an offscreen framebuffer + `vr::IVROverlay::SetOverlayTexture`.

## Logs and profiles

All overlay-side, driver-side, and host-side log files land under `%LocalAppDataLow%\WKOpenVR\Logs\`, one per module per session:

- `spacecal_log.<ts>.txt`
- `smoothing_log.<ts>.txt`
- `inputhealth_log.<ts>.txt`

Profile / config files live under `%LocalAppDataLow%\WKOpenVR\profiles\`. The umbrella's global Logs tab aggregates module streams; each module's own tab includes a Logs section scoped to that module.

Release builds write diagnostics only when debug logging is enabled from the Logs tab. Dev builds force debug logging on so local sessions produce enough evidence for regression work. Calibration logs use `spacecal_log_v2` rows and flush each write to disk.

## Dashboard overlay vs window

The umbrella renders into an offscreen GLFW framebuffer first, then submits the same texture to both the desktop GLFW window and a SteamVR dashboard overlay. Either surface is fully functional; closing the desktop window keeps the dashboard overlay alive. The dashboard overlay icon is `core/src/overlay/dashboard_icon.png`.

A virtual keyboard for in-VR text input is not wired up in this version: the pinned ImGui (`82d0584e7`) is master, not docking, and its `InputText` callbacks operate on UTF-8. SteamVR's `ShowKeyboard` wide-char injection path doesn't round-trip through ImGui cleanly at this pin, so users currently need a physical keyboard to edit text fields while in VR.
