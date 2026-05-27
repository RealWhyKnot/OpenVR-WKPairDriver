# Module: Calibration

Keeps a non-lighthouse HMD (Quest, Pimax, etc.) aligned with lighthouse-tracked full-body trackers. Continuously compares poses from both tracking systems, solves a rigid transform between them, and applies that transform to every tracker so they appear in the correct position relative to the headset. Without this module, SteamVR trackers and the HMD live in separate coordinate frames and the user's virtual feet are in a different room than their head.

Forked from the original OpenVR-SpaceCalibrator. The fork keeps the same Basic/Advanced UI shape so existing users can find familiar controls.

Source: [modules/calibration/](https://github.com/RealWhyKnot/WKOpenVR/tree/main/modules/calibration)

## Driver hooks

- `IVRDriverContext::GetGenericInterface` slot 0 -- MinHook detour intercepts the SteamVR driver host queries during driver startup so the calibration module can attach its `TrackedDevicePoseUpdated` interposer.
- `TrackedDevicePoseUpdated` -- the active interposer rewrites pose updates for non-reference devices through the current calibration transform.

## IPC

`\\.\pipe\OpenVR-Calibration` carries calibration-specific requests:

- `RequestSetDeviceTransform` -- updates the active transform for a device.
- `RequestSetAlignmentSpeedParams` -- pushes the speed thresholds shown on the Advanced tab.
- `RequestSetTrackingSystemFallback` -- chooses the fallback reference when the HMD's tracking system is unavailable.
- `RequestDebugOffset` -- single-shot nudge used by diagnostics.

Driver-side pose telemetry is streamed back to the overlay over the `WKOpenVRPoseMemoryV2` shmem segment.

## Overlay UI

### Basic tab

- **Devices** -- read-only table: reference device (system / model / serial + status: OK / NOT FOUND / NOT TRACKING) and target device on the same row.
- **Actions** -- `Cancel Continuous Calibration` ends continuous mode; `Restart sampling` pushes a random offset so the solver re-searches from fresh samples; `Pause updates` / `Resume updates` freezes the live offset without ending continuous mode (amber highlight while paused).
- **Progress** -- one-shot calibration uses a readiness score instead of a raw sample counter. Rotation progress is capped until rotation coverage is strong enough; translation progress waits for movement coverage across axes.
- **Profile-mismatch banner** -- shown in amber if the saved profile was created with a different HMD tracking system. `Clear profile` and `Recalibrate` buttons.

### Advanced tab

Available in continuous mode.

- **Toggles** -- `Hide tracker` (suppress target tracker's pose so calibration setups don't show a duplicate device); `Ignore outliers` (drop sample pairs whose rotation axis disagrees with the consensus).
- **Calibration speeds** -- radio: Auto / Fast (100 samples) / Slow (250) / Very Slow (500). Auto mode shows resolved speed + jitter readouts inline.
- **Speed thresholds** -- Decel / Slow / Fast x Translation (mm) / Rotation (degrees) sliders; right-click to reset each.
- **Alignment speeds** -- Decel / Slow / Fast rate sliders.
- **Thresholds** -- Jitter, Recalibration, Max relative error.
- **Continuous calibration (advanced)** -- Target latency offset (ms, -100..100) + Auto-detect target latency checkbox.
- **Legacy panel** -- "Legacy (pre-fork upstream math)" master toggle for the pre-fork solve path; useful for regression testing.
- **Diagnostics** -- Watchdog reset count + time; HMD long-stall count + time. Counters highlight amber within 15 s of the last event so transient issues are visible during a session.

### Head-mounted tracker

The head-mounted tracker panel can measure the physical offset between the HMD and a tracker mounted on the headset. Offset collection uses readiness gates for sample count, pitch/yaw/roll coverage, consistency, and residual error. The offset solve is blocked while head-mount pose modes are active because those modes can make the HMD pose depend on the same tracker being measured.

DriverSynth mode keeps continuous calibration running. The driver uses the head-mounted tracker as the primary HMD pose source while the tracker is fresh and valid, then blends back to the headset's own tracking when the tracker becomes stale or invalid. That keeps the normal HMD fallback aligned instead of freezing calibration.

### Quest ADB and Guardian

The Quest setup flow uses bundled platform-tools and labels ADB states as no device, unauthorized, offline, or authorized. USB authorization still requires the user to unlock the headset and accept Android's USB debugging dialog in the headset.

When USB ADB is authorized, the normal wireless path runs `adb tcpip 5555`, discovers the headset Wi-Fi IP over the USB shell, then connects to `<ip>:5555`. Android wireless pairing-code entry remains a manual fallback for headsets that expose it. Saved-endpoint reconnect attempts back off when the endpoint refuses connection.

Guardian controls are applied over ADB and verified by readback. The setup page separates ADB connection state from Guardian state so "connected" and "Guardian active" can be diagnosed independently.

### Safety boundary

The boundary tool lets the user hold an Index controller trigger and paint a floor outline from the controller aim ray. The capture pass scans all controller device IDs, detects the actual trigger axis slot, previews the drawn path, then cleans the saved polygon by removing close-loop tails and collinear points.

### Logs tab

The Logs tab controls debug logging and shows the active `%LocalAppDataLow%\WKOpenVR\Logs\spacecal_log.<ts>.txt` file. The SpaceCal CSV opens as soon as debug logging is enabled, writes a `spacecal_log_v2` header, flushes each row to disk, and records health snapshots, annotations, raw reference/target poses, solve gates, rejection reasons, and profile apply events.

Release builds write these files only while debug logging is enabled. Dev builds force debug logging on and retain a bounded set of replayable recordings for local regression tests.

## Banners / failure modes

- **Profile-mismatch (amber)** -- saved profile's HMD tracking system != current HMD; calibration is not applied. Use the banner's Clear profile button.
- **NOT FOUND** -- device serial not found by SteamVR; tracker is off or not paired.
- **NOT TRACKING** -- device found but not providing valid poses; tracking lost mid-session.
- **Need rotation / need movement** -- continuous calibration has usable data but motion coverage gates are still rejecting candidates. Rotate around more axes or move through a wider range.
- **Watchdog reset (amber for 15 s)** -- solver has been rejecting every sample for ~25 s; the watchdog discarded the in-flight estimate and restarted collection. Usually means the user took the headset off mid-cal.
- **HMD long-stall (amber for 15 s)** -- HMD stopped reporting poses for ~1.5 s+ (headset removed, runtime hiccup).

## Persistence

- Profile + offsets: `%LocalAppDataLow%\WKOpenVR\profiles\<profile>.json` (one profile per HMD tracking system).
- Session log and replay CSV: `%LocalAppDataLow%\WKOpenVR\Logs\spacecal_log.<ts>.txt`.

## Tests

`modules/calibration/tests/spacecal_tests.exe` covers the solver, readiness gates, motion-quality rejection gates, watchdog behavior, boundary capture, Guardian setup helpers, DriverSynth composition, retained motion recordings, and the protocol version pin.
