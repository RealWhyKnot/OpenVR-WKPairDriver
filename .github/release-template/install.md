## Install (fresh)

Easiest path -- run an NSIS installer (handles Program Files, SteamVR registration, Start Menu shortcut, and the uninstaller). All installers ship on this release page:

- **WKOpenVR (all features available)**: `WKOpenVR-v{version}-Setup.exe`. No feature is enabled by default; toggle the ones you want from the Modules tab inside WKOpenVR after install.
- **Room calibration only**: `WKOpenVR-Calibration-v{version}-Setup.exe` -- pre-enables calibration.
- **Finger smoothing only**: `WKOpenVR-Smoothing-v{version}-Setup.exe` -- pre-enables smoothing.
- **Input health monitoring only**: `WKOpenVR-InputHealth-v{version}-Setup.exe` -- pre-enables input health.

Manual extract path -- download `WKOpenVR-{version}.zip` and extract into `<SteamVR runtime>\drivers\01wkopenvr\`. The zip drops no flags; drop the `enable_<feature>.flag` files you want into `drivers\01wkopenvr\resources\` yourself. Restart SteamVR. The driver loads the features whose flag files it finds there.
