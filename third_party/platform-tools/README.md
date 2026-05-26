Android platform-tools (Windows) -- bundled for ADB Guardian-pause feature

Version: 37.0.0
Source:  https://dl.google.com/android/repository/platform-tools-latest-windows.zip

Files included:
  adb.exe          -- Android Debug Bridge command-line tool
  AdbWinApi.dll    -- ADB Windows USB API library
  AdbWinUsbApi.dll -- ADB Windows USB API library (extended)
  adb-LICENSE.txt  -- Apache-2.0 license (NOTICE.txt from the upstream zip)

License:
  Apache License 2.0. See adb-LICENSE.txt.
  Compatible with this project's GPL-3.0 license per GPL-3.0 section 7.

These binaries are installed to <install-root>/bin/adb/ by the installer and
the dev deploy script. AdbController resolves them relative to the overlay
executable's directory at runtime.

Upgrading:
  Download the latest zip from the URL above, replace the four files in this
  directory, update Pkg.Revision in source.properties (not committed), and
  update the version line in this README. Surface the version in the
  diagnostics panel so users can see which build is active.
