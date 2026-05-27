#pragma once

// Pure helper for driver-level HMD pose synthesis from a head-mounted tracker.
//
// Extracted from ServerTrackedDeviceProvider::HandleDevicePoseUpdated so unit
// tests can exercise the compose/gate logic without a live OpenVR runtime.
// All functions are pure: no static state, no global CalCtx access, no vr::*
// calls beyond struct field access.
//
// Only compiled into dev builds (WKOPENVR_BUILD_IS_DEV == 1); the synthesis
// branch in the hook is gated by the same macro. Including this header in a
// release build is harmless -- the structs and inline functions compile away
// -- but the driver never calls into this path.

#include <chrono>
#include <cstdint>

#include <openvr_driver.h>

namespace driver_synth {

// Parameters captured from the cached HeadMountDriverState. Plain POD so the
// caller can copy them out of the mutex-guarded state in one memcpy and pass
// a stack copy here.
struct SynthState {
    int     mode;                   // HeadMountMode integer; 3 = DriverSynth
    int32_t deviceId;               // -1 means unresolved
    double  headFromTrackerTrans[3];
    double  headFromTrackerRot[4];  // xyzw
    bool    offsetCalibrated;
};

// A snapshot of a tracker pose and when it was captured. capturedForDeviceId
// records which deviceId the snapshot was filed under so the synth path can
// reject snapshots written for a previously-selected tracker after the user
// switches which device drives the head-mount.
struct TrackerSnapshot {
    vr::DriverPose_t pose;
    std::chrono::steady_clock::time_point capturedAt;
    int32_t capturedForDeviceId = -1;
    bool valid = false;             // false until at least one pose is stored
};

// Freshness limit. Any tracker snapshot older than this is treated as stale.
constexpr int64_t kStaleLimitMs = 30;

// Returns true when the snapshot is present, valid, and not older than
// kStaleLimitMs relative to `now`.
inline bool IsTrackerFresh(const TrackerSnapshot& snap,
                            std::chrono::steady_clock::time_point now)
{
    if (!snap.valid) return false;
    if (!snap.pose.poseIsValid) return false;
    using ms = std::chrono::milliseconds;
    return std::chrono::duration_cast<ms>(now - snap.capturedAt).count()
           <= kStaleLimitMs;
}

// Compose a synthesized HMD pose from the tracker pose and state.
//
// Returns true and writes into `out` when all preconditions hold:
//   - state.mode == 3 (DriverSynth)
//   - state.deviceId >= 0 (tracker resolved)
//   - state.offsetCalibrated (offset has been through the solver)
//   - trackerSnap.valid && trackerSnap.pose.poseIsValid
//   - tracker snapshot not stale
//
// Returns false (pass through) on any failure.
inline bool Compose(const SynthState& state,
                    const TrackerSnapshot& trackerSnap,
                    std::chrono::steady_clock::time_point now,
                    vr::DriverPose_t& out)
{
    // Mode and resolver gate.
    if (state.mode != 3 /*DriverSynth*/) return false;
    if (state.deviceId < 0) return false;
    if (!state.offsetCalibrated) return false;

    // Tracker freshness.
    if (!IsTrackerFresh(trackerSnap, now)) return false;

    // Reject snapshots captured for a different device. If the user switched
    // which tracker drives the head-mount between snapshot write and synth
    // read, the cached pose belongs to the previous device and cannot be
    // safely composed against the current state's offset.
    if (trackerSnap.capturedForDeviceId != state.deviceId) return false;

    const vr::DriverPose_t& trackerPose = trackerSnap.pose;

    // Build the synthesized pose. Start from the tracker pose to inherit
    // position, rotation, velocities, poseTimeOffset, validity fields, and the
    // calibrated worldFromDriver transform supplied by the driver call site.
    out = trackerPose;

    // Override the driver-from-head transform with our configured offset.
    // This is the OpenVR-native way to express "tracker origin -> IPD midpoint"
    // per the Driver API Documentation: qDriverFromHeadRotation and
    // vecDriverFromHeadTranslation encode how to get from the driver's local
    // coordinate frame (the tracker) to the "head" (IPD midpoint) pose that
    // SteamVR renders from.
    out.qDriverFromHeadRotation.x = state.headFromTrackerRot[0];
    out.qDriverFromHeadRotation.y = state.headFromTrackerRot[1];
    out.qDriverFromHeadRotation.z = state.headFromTrackerRot[2];
    out.qDriverFromHeadRotation.w = state.headFromTrackerRot[3];
    out.vecDriverFromHeadTranslation[0] = state.headFromTrackerTrans[0];
    out.vecDriverFromHeadTranslation[1] = state.headFromTrackerTrans[1];
    out.vecDriverFromHeadTranslation[2] = state.headFromTrackerTrans[2];

    return true;
}

} // namespace driver_synth
