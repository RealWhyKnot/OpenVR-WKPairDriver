#pragma once

namespace openvr_pair::overlay {

// One-shot registration of WKOpenVR's vrmanifest with SteamVR. Registers
// manifest.vrmanifest (sitting next to WKOpenVR.exe) if the app key is not
// already installed, and sets autolaunch so SteamVR opens the overlay on the
// next session start. Best-effort removal of the legacy steam.overlay.3368750
// registration (the SC standalone exe that no longer exists) keeps the SteamVR
// overlay list clean.
//
// When allowRuntimeLaunch is false, this uses background-mode OpenVR init so
// normal desktop launches do not start SteamVR.
void RegisterApplicationManifest(bool allowRuntimeLaunch = true);

// Best-effort removal of the WKOpenVR vrmanifest registration. Called by
// the NSIS uninstaller via "WKOpenVR.exe --unregister-only" BEFORE the
// installed exe + manifest files are deleted, so SteamVR does not end up
// trying to autolaunch a missing binary on the next session.
void UnregisterApplicationManifest();

} // namespace openvr_pair::overlay
