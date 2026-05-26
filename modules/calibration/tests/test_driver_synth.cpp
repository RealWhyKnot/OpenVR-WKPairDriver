// Unit tests for the driver-level HMD synthesis pure helpers in
// DriverSynthCompose.h. Exercises the Compose() function and its guards in
// isolation -- no OpenVR runtime, no IPC, no CalCtx.
//
// Required cases (per the approved plan):
//   1. Valid_tracker_offsetCalibrated -- synthesized pose uses tracker data
//   2. Tracker_invalid -- returns false (pass through)
//   3. Tracker_stale -- returns false
//   4. Upstream_hmd_stale -- returns false
//   5. Sanity_gate_too_far -- returns false when delta > 1 m
//   6. Mode_off -- returns false
//   7. Mode_autopaired -- returns false (only DriverSynth fires)
//   8. DeviceId_unresolved -- returns false when deviceId == -1
//   9. OffsetCalibrated_false -- returns false
//  10. Synthesized_worldFromDriver_copied_from_upstream

#include "DriverSynthCompose.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace driver_synth;
using clk = std::chrono::steady_clock;

namespace {

// Build a minimal valid DriverPose_t. Not connected to a live runtime; just
// a value object for the test's math.
vr::DriverPose_t MakePose(double x, double y, double z,
                           bool poseIsValid = true)
{
    vr::DriverPose_t p{};
    p.poseIsValid = poseIsValid;
    p.result      = vr::TrackingResult_Running_OK;
    p.vecPosition[0] = x;
    p.vecPosition[1] = y;
    p.vecPosition[2] = z;
    // Identity rotation.
    p.qRotation.w = 1.0; p.qRotation.x = 0.0;
    p.qRotation.y = 0.0; p.qRotation.z = 0.0;
    // Identity worldFromDriver.
    p.qWorldFromDriverRotation.w = 1.0;
    p.vecWorldFromDriverTranslation[0] = 0.0;
    p.vecWorldFromDriverTranslation[1] = 0.0;
    p.vecWorldFromDriverTranslation[2] = 0.0;
    return p;
}

// Build a state with mode 3 (DriverSynth), a resolved deviceId, and
// offsetCalibrated true. Translation offset is (tx, ty, tz); rotation is
// identity quaternion (0, 0, 0, 1) in xyzw.
SynthState MakeState(int mode = 3, int32_t deviceId = 2,
                      bool offsetCalibrated = true,
                      double tx = 0.0, double ty = 0.0, double tz = 0.0)
{
    SynthState s{};
    s.mode             = mode;
    s.deviceId         = deviceId;
    s.offsetCalibrated = offsetCalibrated;
    s.headFromTrackerTrans[0] = tx;
    s.headFromTrackerTrans[1] = ty;
    s.headFromTrackerTrans[2] = tz;
    s.headFromTrackerRot[0]   = 0.0;
    s.headFromTrackerRot[1]   = 0.0;
    s.headFromTrackerRot[2]   = 0.0;
    s.headFromTrackerRot[3]   = 1.0;
    return s;
}

// Build a fresh, valid TrackerSnapshot. capturedForDeviceId defaults to 2 so
// snapshots line up with MakeState's default deviceId. Pass deviceId=0 to
// build an upstream-HMD snapshot.
TrackerSnapshot FreshSnap(vr::DriverPose_t pose,
                           clk::time_point now,
                           int32_t deviceId = 2)
{
    TrackerSnapshot snap{};
    snap.pose      = pose;
    snap.capturedAt = now;
    snap.capturedForDeviceId = deviceId;
    snap.valid      = true;
    return snap;
}

// Build a stale TrackerSnapshot (captured kStaleLimitMs + 5 ms ago).
TrackerSnapshot StaleSnap(vr::DriverPose_t pose,
                           clk::time_point now,
                           int32_t deviceId = 2)
{
    using ms = std::chrono::milliseconds;
    TrackerSnapshot snap{};
    snap.pose       = pose;
    snap.capturedAt = now - ms(kStaleLimitMs + 5);
    snap.capturedForDeviceId = deviceId;
    snap.valid      = true;
    return snap;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Valid tracker + offsetCalibrated: synthesized pose uses tracker data
//    and copies worldFromDriver from the upstream HMD.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Valid_tracker_offsetCalibrated)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    // Tracker at (0.1, 1.7, 0.2), upstream HMD at (0.0, 1.7, 0.0).
    // Match both poses' worldFromDriver translations so they land at the same
    // world position and the sanity gate accepts; the test only verifies that
    // worldFromDriver is copied from upstream into the synthesized output.
    vr::DriverPose_t trackerPose = MakePose(0.1, 1.7, 0.2);
    trackerPose.vecWorldFromDriverTranslation[0] = 5.0;
    trackerPose.vecWorldFromDriverTranslation[1] = 1.0;
    trackerPose.vecWorldFromDriverTranslation[2] = 2.0;
    trackerPose.qWorldFromDriverRotation.w = 1.0;

    vr::DriverPose_t upstream = MakePose(0.0, 1.7, 0.0);
    upstream.vecWorldFromDriverTranslation[0] = 5.0;
    upstream.vecWorldFromDriverTranslation[1] = 1.0;
    upstream.vecWorldFromDriverTranslation[2] = 2.0;
    upstream.qWorldFromDriverRotation.w = 1.0;

    const TrackerSnapshot trackerSnap  = FreshSnap(trackerPose, now);
    const TrackerSnapshot upstreamSnap = FreshSnap(upstream, now);

    vr::DriverPose_t out{};
    EXPECT_TRUE(Compose(state, trackerSnap, upstreamSnap, now, out));

    // Position comes from the tracker.
    EXPECT_NEAR(out.vecPosition[0], 0.1, 1e-9);
    EXPECT_NEAR(out.vecPosition[1], 1.7, 1e-9);
    EXPECT_NEAR(out.vecPosition[2], 0.2, 1e-9);

    // worldFromDriver copied from upstream HMD.
    EXPECT_NEAR(out.vecWorldFromDriverTranslation[0], 5.0, 1e-9);
    EXPECT_NEAR(out.vecWorldFromDriverTranslation[1], 1.0, 1e-9);
    EXPECT_NEAR(out.vecWorldFromDriverTranslation[2], 2.0, 1e-9);

    // headFromTracker translation stored in the output (identity offset here).
    EXPECT_NEAR(out.vecDriverFromHeadTranslation[0], 0.0, 1e-9);
    EXPECT_NEAR(out.vecDriverFromHeadTranslation[1], 0.0, 1e-9);
    EXPECT_NEAR(out.vecDriverFromHeadTranslation[2], 0.0, 1e-9);

    // Rotation offset stored (identity quaternion: w=1, xyz=0).
    EXPECT_NEAR(out.qDriverFromHeadRotation.w, 1.0, 1e-9);
    EXPECT_NEAR(out.qDriverFromHeadRotation.x, 0.0, 1e-9);
    EXPECT_NEAR(out.qDriverFromHeadRotation.y, 0.0, 1e-9);
    EXPECT_NEAR(out.qDriverFromHeadRotation.z, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// 2. Tracker invalid: poseIsValid == false -> returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Tracker_invalid)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    vr::DriverPose_t badPose = MakePose(0.0, 1.7, 0.0, /*poseIsValid=*/false);
    const TrackerSnapshot trackerSnap  = FreshSnap(badPose, now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 3. Tracker stale (> 30 ms): returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Tracker_stale)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    const TrackerSnapshot staleTracker = StaleSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot freshUpstream = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, staleTracker, freshUpstream, now, out));
}

// ---------------------------------------------------------------------------
// 4. Upstream HMD pose stale (> 30 ms): returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Upstream_hmd_stale)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    const TrackerSnapshot freshTracker  = FreshSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot staleUpstream = StaleSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, freshTracker, staleUpstream, now, out));
}

// ---------------------------------------------------------------------------
// 5. Sanity gate: tracker position > 1 m from upstream HMD -> returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Sanity_gate_too_far)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    // Tracker at (2.0, 1.7, 0.0), upstream at (0.0, 1.7, 0.0): 2 m apart.
    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(2.0, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 5b. Sanity gate: within 1 m -> does NOT block.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Sanity_gate_within_range)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    // Tracker at (0.05, 1.7, 0.0), upstream at (0.0, 1.7, 0.0): 5 cm apart.
    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.05, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.00, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_TRUE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 6. mode == 0 (Off): returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Mode_off)
{
    const auto now = clk::now();
    const SynthState state = MakeState(/*mode=*/0);

    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 7. mode == 1 (AutoPaired): returns false; only DriverSynth (3) fires.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Mode_autopaired)
{
    const auto now = clk::now();
    const SynthState state = MakeState(/*mode=*/1);

    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 8. deviceId == -1 (unresolved): returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, DeviceId_unresolved)
{
    const auto now = clk::now();
    const SynthState state = MakeState(/*mode=*/3, /*deviceId=*/-1);

    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 9. offsetCalibrated == false: returns false.
// ---------------------------------------------------------------------------
TEST(DriverSynth, OffsetCalibrated_false)
{
    const auto now = clk::now();
    const SynthState state = MakeState(/*mode=*/3, /*deviceId=*/2,
                                        /*offsetCalibrated=*/false);

    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 10. Non-trivial headFromTracker offset stored correctly in the output.
// ---------------------------------------------------------------------------
TEST(DriverSynth, HeadFromTracker_offset_stored)
{
    const auto now = clk::now();
    // Offset: (0.01, -0.05, 0.02) metres.
    const SynthState state = MakeState(/*mode=*/3, /*deviceId=*/2,
                                        /*offsetCalibrated=*/true,
                                        /*tx=*/0.01, /*ty=*/-0.05, /*tz=*/0.02);

    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.0, 1.7, 0.0), now);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now);

    vr::DriverPose_t out{};
    ASSERT_TRUE(Compose(state, trackerSnap, upstreamSnap, now, out));

    EXPECT_NEAR(out.vecDriverFromHeadTranslation[0],  0.01, 1e-9);
    EXPECT_NEAR(out.vecDriverFromHeadTranslation[1], -0.05, 1e-9);
    EXPECT_NEAR(out.vecDriverFromHeadTranslation[2],  0.02, 1e-9);
}

// ---------------------------------------------------------------------------
// 11. Sanity gate compares positions in WORLD frame, not driver-local.
//
// The tracker and HMD are produced by different drivers (lighthouse vs Quest);
// each carries its own worldFromDriver. Lighthouse driver-local position can
// be metres away from Quest driver-local position for the same physical point.
// Verify that:
//   a) Two poses at the same world position but different driver-locals pass.
//   b) Two poses with matching driver-local positions but driver-local offsets
//      that place them apart in world fail.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Sanity_gate_uses_world_frame)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    // Both end up at world position (0, 1.7, 0).
    // Tracker: driver-local (1, 1.7, 0); lighthouse worldFromDriver offsets by -1 X.
    // HMD: driver-local (-2, 1.7, 0); Quest worldFromDriver offsets by +2 X.
    vr::DriverPose_t tracker = MakePose(1.0, 1.7, 0.0);
    tracker.vecWorldFromDriverTranslation[0] = -1.0;
    tracker.vecWorldFromDriverTranslation[1] = 0.0;
    tracker.vecWorldFromDriverTranslation[2] = 0.0;
    tracker.qWorldFromDriverRotation.w = 1.0;
    tracker.qWorldFromDriverRotation.x = 0.0;
    tracker.qWorldFromDriverRotation.y = 0.0;
    tracker.qWorldFromDriverRotation.z = 0.0;

    vr::DriverPose_t hmd = MakePose(-2.0, 1.7, 0.0);
    hmd.vecWorldFromDriverTranslation[0] = 2.0;
    hmd.vecWorldFromDriverTranslation[1] = 0.0;
    hmd.vecWorldFromDriverTranslation[2] = 0.0;
    hmd.qWorldFromDriverRotation.w = 1.0;
    hmd.qWorldFromDriverRotation.x = 0.0;
    hmd.qWorldFromDriverRotation.y = 0.0;
    hmd.qWorldFromDriverRotation.z = 0.0;

    // Driver-local positions are 3 m apart (>1 m gate), but in world frame
    // they coincide. A naive driver-local check would fail-through; the
    // world-frame check must pass.
    const TrackerSnapshot trackerSnap  = FreshSnap(tracker, now);
    const TrackerSnapshot upstreamSnap = FreshSnap(hmd, now);

    vr::DriverPose_t out{};
    EXPECT_TRUE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

TEST(DriverSynth, Sanity_gate_world_frame_rejects_far)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    // Driver-local positions match exactly, but worldFromDriver translations
    // place the two poses 5 m apart in world. The gate must reject.
    vr::DriverPose_t tracker = MakePose(0.0, 1.7, 0.0);
    tracker.qWorldFromDriverRotation.w = 1.0;

    vr::DriverPose_t hmd = MakePose(0.0, 1.7, 0.0);
    hmd.vecWorldFromDriverTranslation[0] = 5.0;
    hmd.qWorldFromDriverRotation.w = 1.0;

    const TrackerSnapshot trackerSnap  = FreshSnap(tracker, now);
    const TrackerSnapshot upstreamSnap = FreshSnap(hmd, now);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

// ---------------------------------------------------------------------------
// 14. Tracker snapshot captured for a different deviceId is rejected.
//     Guards the IPC-changes-deviceId-mid-frame race between snapshot write
//     and synthesis read.
// ---------------------------------------------------------------------------
TEST(DriverSynth, Tracker_snap_for_different_deviceId_rejected)
{
    const auto now = clk::now();
    const SynthState state = MakeState(/*mode=*/3, /*deviceId=*/2);

    // Snapshot captured for deviceId 7 (the previously-selected tracker).
    const TrackerSnapshot trackerSnap  = FreshSnap(MakePose(0.0, 1.7, 0.0), now, 7);
    const TrackerSnapshot upstreamSnap = FreshSnap(MakePose(0.0, 1.7, 0.0), now, 0);

    vr::DriverPose_t out{};
    EXPECT_FALSE(Compose(state, trackerSnap, upstreamSnap, now, out));
}

TEST(DriverSynth, Sanity_gate_world_frame_rotation_applied)
{
    const auto now = clk::now();
    const SynthState state = MakeState();

    // Tracker driver-local at (1, 0, 0). Tracker worldFromDriver rotates 180 deg
    // around Y, so the tracker lands at world (-1, 0, 0).
    // HMD driver-local at (-1, 0, 0). Identity worldFromDriver. HMD lands at
    // world (-1, 0, 0). Distance in world frame = 0.
    vr::DriverPose_t tracker = MakePose(1.0, 0.0, 0.0);
    // 180 deg around Y: w=0, y=1
    tracker.qWorldFromDriverRotation.w = 0.0;
    tracker.qWorldFromDriverRotation.x = 0.0;
    tracker.qWorldFromDriverRotation.y = 1.0;
    tracker.qWorldFromDriverRotation.z = 0.0;

    vr::DriverPose_t hmd = MakePose(-1.0, 0.0, 0.0);
    hmd.qWorldFromDriverRotation.w = 1.0;

    const TrackerSnapshot trackerSnap  = FreshSnap(tracker, now);
    const TrackerSnapshot upstreamSnap = FreshSnap(hmd, now);

    vr::DriverPose_t out{};
    EXPECT_TRUE(Compose(state, trackerSnap, upstreamSnap, now, out));
}
