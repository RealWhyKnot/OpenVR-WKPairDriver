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
#include <cmath>
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

// Freshness limit. Any snapshot older than this is treated as stale and
// triggers the fallback path.
constexpr int64_t kStaleLimitMs = 30;

// Sanity gate: synthesized HMD position further than this from the upstream
// Quest HMD position triggers the fallback path. Guards against a bad mount
// calibration shipping the user's rendered head to a wildly wrong location.
constexpr double kMaxSynthDeltaM = 1.0;

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

// Returns true when the cached upstream HMD snapshot is not older than
// kStaleLimitMs relative to `now`. The upstream pose is needed for its
// worldFromDriver transform and as the sanity-gate reference.
inline bool IsUpstreamHmdFresh(const TrackerSnapshot& hmdSnap,
                                std::chrono::steady_clock::time_point now)
{
    return IsTrackerFresh(hmdSnap, now);
}

// Compose a synthesized HMD pose from the tracker pose and state.
//
// Returns true and writes into `out` when all preconditions hold:
//   - state.mode == 3 (DriverSynth)
//   - state.deviceId >= 0 (tracker resolved)
//   - state.offsetCalibrated (offset has been through the solver)
//   - trackerSnap.valid && trackerSnap.pose.poseIsValid
//   - tracker snapshot not stale
//   - upstream HMD snapshot not stale
//   - synthesized position within kMaxSynthDeltaM of upstream HMD position
//
// Returns false (pass through) on any failure.
inline bool Compose(const SynthState& state,
                    const TrackerSnapshot& trackerSnap,
                    const TrackerSnapshot& upstreamHmdSnap,
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

    // Upstream HMD freshness (needed for worldFromDriver and sanity gate).
    if (!IsUpstreamHmdFresh(upstreamHmdSnap, now)) return false;

    const vr::DriverPose_t& trackerPose = trackerSnap.pose;
    const vr::DriverPose_t& upstreamHmd = upstreamHmdSnap.pose;

    // Sanity gate in world frame. The tracker and HMD are produced by different
    // drivers with independent driver-local origins -- comparing raw vecPosition
    // values is meaningless when Quest and lighthouse universes do not coincide.
    // Lift each pose through its own worldFromDriver (quaternion rotation +
    // translation) and check the world-space distance.
    auto rotateAndTranslate = [](const vr::HmdQuaternion_t& q,
                                  const double* t,
                                  const double* v,
                                  double out[3]) {
        // p' = q * v * q^-1 + t  -- quaternion-vector rotation, then offset.
        const double qw = q.w, qx = q.x, qy = q.y, qz = q.z;
        const double vx = v[0], vy = v[1], vz = v[2];
        // r = q * v (with v as pure quaternion (0, vx, vy, vz))
        const double rw = -qx*vx - qy*vy - qz*vz;
        const double rx =  qw*vx + qy*vz - qz*vy;
        const double ry =  qw*vy + qz*vx - qx*vz;
        const double rz =  qw*vz + qx*vy - qy*vx;
        // result = r * q^-1
        out[0] = -rw*qx + rx*qw - ry*qz + rz*qy + t[0];
        out[1] = -rw*qy + ry*qw - rz*qx + rx*qz + t[1];
        out[2] = -rw*qz + rz*qw - rx*qy + ry*qx + t[2];
    };

    double trackerWorld[3];
    double hmdWorld[3];
    rotateAndTranslate(trackerPose.qWorldFromDriverRotation,
                       trackerPose.vecWorldFromDriverTranslation,
                       trackerPose.vecPosition, trackerWorld);
    rotateAndTranslate(upstreamHmd.qWorldFromDriverRotation,
                       upstreamHmd.vecWorldFromDriverTranslation,
                       upstreamHmd.vecPosition, hmdWorld);
    const double dx = trackerWorld[0] - hmdWorld[0];
    const double dy = trackerWorld[1] - hmdWorld[1];
    const double dz = trackerWorld[2] - hmdWorld[2];
    if (std::sqrt(dx*dx + dy*dy + dz*dz) > kMaxSynthDeltaM) return false;

    // Build the synthesized pose. Start from the tracker pose to inherit
    // position, rotation, velocities, poseTimeOffset, and validity fields.
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

    // Keep worldFromDriver from the upstream Quest HMD pose so the SteamVR
    // universe (room setup, chaperone, app coordinate systems) is preserved.
    // The synthesized pose lives in Quest's universe: Quest drift is NOT
    // eliminated, but within that universe the rendered head tracks the
    // lighthouse tracker instead of Quest SLAM.
    out.qWorldFromDriverRotation    = upstreamHmd.qWorldFromDriverRotation;
    out.vecWorldFromDriverTranslation[0] = upstreamHmd.vecWorldFromDriverTranslation[0];
    out.vecWorldFromDriverTranslation[1] = upstreamHmd.vecWorldFromDriverTranslation[1];
    out.vecWorldFromDriverTranslation[2] = upstreamHmd.vecWorldFromDriverTranslation[2];

    return true;
}

} // namespace driver_synth
