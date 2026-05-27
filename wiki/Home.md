# WKOpenVR

Umbrella SteamVR overlay + driver. One binary (`WKOpenVR.exe`) and one driver DLL (`driver_wkopenvr.dll`) host the release modules under `modules/`. Each module is toggled on or off via a marker flag file the overlay's Modules tab manages.

## Modules in release

- **Calibration** -- align lighthouse-tracked trackers with a non-lighthouse HMD. ([deep-dive](Module-Calibration))
- **Smoothing** -- finger-curl smoothing and per-tracker pose-prediction control for Valve Index Knuckles. ([deep-dive](Module-Smoothing))
- **InputHealth** -- drift and degradation detection for buttons / axes / fingers, with learned compensation. ([deep-dive](Module-InputHealth))

## In development

These modules live under `modules/` and build in dev (`./build.ps1` with no `-Release` flag), but are excluded from published release artifacts via `modules/<slug>/disabled-in-release.flag` until they stabilise.

- **OSCRouter** -- OSC fan-out of pose / tracker / chatbox data to multiple downstream consumers.
- **Captions** -- on-device speech recognition and translation via a whisper.cpp + CTranslate2 host sidecar.
- **Phantom** -- tracker dropout dead-reckoning, IK fallback, and virtual-tracker placeholders, with an out-of-process ML pose-completion sidecar.

## Reference

- [Architecture](Architecture) -- shared driver DLL, module composition, feature flags, marker files.
- [Build](Build) -- toolchain prerequisites, CMake invocation, host opt-out switch.
- [Protocol](Protocol) -- named pipes, shared-memory rings, `Protocol.h` reference, version-bump discipline.
- [Release process](Release-Process) -- how `release.yml` produces the umbrella zip + per-feature installers.
- [Changelog](Changelog) -- auto-appended from conventional-commit subjects; tagged sections promoted on release.

## Source

[WKOpenVR](https://github.com/RealWhyKnot/WKOpenVR) on GitHub.
