// Warm-restart engage-decision tests. Pins the conditions under which a
// proximity-sensor false -> true edge engages the saved-profile snap path
// described in WarmRestart.h. The fix landed because a real session
// (spacecal_log.2026-05-25T14-05-35.txt) took ~7 minutes to settle after
// the user set the HMD down for a drink and came back, with the avatar
// "flying away and flying back" until the rolling sample buffer refilled
// against the saved offset. Each test below corresponds to one of the
// gate's input conditions; if any of them inverts in the future the
// engage path either over-fires (snaps on a sensor blip) or under-fires
// (drops back into the slow re-validation that this fix exists to skip).

#include "WarmRestart.h"

#include <gtest/gtest.h>

namespace wr = spacecal::warm_restart;

namespace {

// Baseline "healthy warm restart" input -- all conditions met. Individual
// tests below flip one field at a time to confirm each gate fires alone.
constexpr wr::EngageInput kHealthyWarmRestart = {
    /*wasPresent=*/false,
    /*nowPresent=*/true,
    /*awayForSeconds=*/30.0,
    /*validProfile=*/true,
    /*stateEligible=*/true,
};

}  // namespace

// --- Rising edge requirement ------------------------------------------------

TEST(WarmRestartTest, EngagesOnRisingEdge) {
    EXPECT_TRUE(wr::ShouldEngage(kHealthyWarmRestart));
}

TEST(WarmRestartTest, DoesNotEngageOnFallingEdge) {
    // User just took the HMD off. We saved away-since-now elsewhere; this
    // tick should not engage anything.
    wr::EngageInput in = kHealthyWarmRestart;
    in.wasPresent = true;
    in.nowPresent = false;
    EXPECT_FALSE(wr::ShouldEngage(in));
}

TEST(WarmRestartTest, DoesNotEngageOnSteadyTrue) {
    // User is wearing the HMD continuously -- no edge to act on.
    wr::EngageInput in = kHealthyWarmRestart;
    in.wasPresent = true;
    in.nowPresent = true;
    EXPECT_FALSE(wr::ShouldEngage(in));
}

TEST(WarmRestartTest, DoesNotEngageOnSteadyFalse) {
    // HMD sitting on a desk; no one is wearing it. No edge.
    wr::EngageInput in = kHealthyWarmRestart;
    in.wasPresent = false;
    in.nowPresent = false;
    EXPECT_FALSE(wr::ShouldEngage(in));
}

// --- Minimum-away threshold -------------------------------------------------

// A sub-threshold blip should be filtered out -- some HMD runtimes report
// brief proximity drops on radio glitches that don't reflect the user
// removing the headset. Without this gate, every passing glitch would
// trigger a snap + grace cycle and re-disturb the calibration.
TEST(WarmRestartTest, DoesNotEngageOnSubThresholdBlip) {
    wr::EngageInput in = kHealthyWarmRestart;
    in.awayForSeconds = 1.0;
    EXPECT_FALSE(wr::ShouldEngage(in))
        << "Brief proximity drops are sensor noise, not real warm restarts";
}

TEST(WarmRestartTest, EngagesAtExactlyThreshold) {
    // The threshold is inclusive: a clean kMinAwaySeconds away counts.
    // Don't widen this inadvertently -- the user-visible latency between
    // putting the HMD back on and feeling tracked correctly is bounded
    // below by this value (you can't snap until you're sure it's a real
    // wake, but the user is staring at "flying away" the whole time).
    wr::EngageInput in = kHealthyWarmRestart;
    in.awayForSeconds = wr::kMinAwaySeconds;
    EXPECT_TRUE(wr::ShouldEngage(in));
}

TEST(WarmRestartTest, DoesNotEngageJustBelowThreshold) {
    wr::EngageInput in = kHealthyWarmRestart;
    in.awayForSeconds = wr::kMinAwaySeconds - 0.01;
    EXPECT_FALSE(wr::ShouldEngage(in));
}

// --- Valid-profile requirement ----------------------------------------------

// Without a saved profile there's nothing to snap to. Engaging the grace
// window here would just bypass the gate for the first calibration ever,
// silently accepting whatever the solver produces in the first 30 s --
// a regression risk for fresh installs where the user is still calibrating
// for the first time.
TEST(WarmRestartTest, DoesNotEngageWithoutValidProfile) {
    wr::EngageInput in = kHealthyWarmRestart;
    in.validProfile = false;
    EXPECT_FALSE(wr::ShouldEngage(in));
}

// --- State eligibility ------------------------------------------------------

// One-shot wizard phases (Begin / Rotation / Translation / Editing) all
// have their own UX and motion gates; the warm-restart fast path is for
// continuous mode only. The non-continuous case isn't expressible in this
// pure helper without enumerating CalibrationState; the caller flattens
// the membership check to a single bool. This test pins the contract that
// when the caller says "not eligible," engage stays off.
TEST(WarmRestartTest, DoesNotEngageWhenStateNotEligible) {
    wr::EngageInput in = kHealthyWarmRestart;
    in.stateEligible = false;
    EXPECT_FALSE(wr::ShouldEngage(in));
}

// --- Compile-time sanity ----------------------------------------------------

// ShouldEngage is constexpr because the input is all PODs and the logic
// is pure boolean. Pinning the constexpr evaluations keeps anyone from
// silently breaking constexpr-eligibility by, e.g., adding logging or a
// non-constexpr helper inside the gate.
static_assert(wr::ShouldEngage(kHealthyWarmRestart),
    "Healthy input must evaluate to engage at compile time");
static_assert(!wr::ShouldEngage({false, false, 30.0, true, true}),
    "No rising edge means no engage, at compile time");
static_assert(!wr::ShouldEngage({false, true, 1.0, true, true}),
    "Sub-threshold away duration means no engage, at compile time");
static_assert(!wr::ShouldEngage({false, true, 30.0, false, true}),
    "Missing saved profile means no engage, at compile time");
static_assert(!wr::ShouldEngage({false, true, 30.0, true, false}),
    "Wrong state means no engage, at compile time");

// --- Threshold values pinned ------------------------------------------------

// kMinAwaySeconds and kGraceSamples both get tuned by reading session logs;
// pin the current values so a change is forced through the test suite as a
// signal that the tuning rationale should be documented.
static_assert(wr::kMinAwaySeconds == 5.0,
    "kMinAwaySeconds changed -- update calibration_robustness memos");
static_assert(wr::kGraceSamples == 100,
    "kGraceSamples changed -- ~30 s of grace at 3.5 Hz; verify the new "
    "value with the latest tick-rate measurement");
