#pragma once

// Pure helpers for the warm-restart snap path -- detects "user took the HMD
// off, came back, put it on again" via the proximity-sensor rising edge and
// decides whether to engage the saved-profile snap + acceptance-gate grace
// window. Extracted here so the threshold / eligibility logic can be unit-
// tested without standing up the full CalibrationTick + IVRSystem stubs.
// See CalibrationContext::warmRestartGraceSamples for the runtime state
// and Calibration.cpp's CalibrationTick for the call site.

namespace spacecal::warm_restart {

// Minimum proximity-false duration that counts as a real break. Below this,
// the rising edge is treated as sensor noise (some HMD runtimes report brief
// drops on radio glitches that don't reflect the user removing the headset).
// 5 s is well past any single-tick blip while still feeling immediate when
// the user actually does step away for a moment.
constexpr double kMinAwaySeconds = 5.0;

// Number of Continuous-mode ticks to keep the prior-vs-new error rejection
// gate bypassed once a warm restart engages. At the calibrator's ~3.5 Hz
// continuous-tick cadence this is ~30 s of grace -- long enough for the
// rolling sample buffer to refill and the solver to converge against the
// saved transform, short enough that a wrong snap (user moved a base station
// while away) is bounded in how long the bad calibration sits there before
// the normal continuous-cal recovery takes over.
constexpr int kGraceSamples = 100;

// Inputs to the engage decision. Built once per tick from the live OpenVR
// proximity reading + the cached CalibrationContext state. `awayForSeconds`
// is the wall-clock duration the user has been away; callers pass 0 when
// they don't have a previous "away" timestamp (no edge to evaluate).
struct EngageInput {
    bool   wasPresent;        // ctx.lastUserPresent before this tick
    bool   nowPresent;        // freshly-read Prop_UserPresent_Bool
    double awayForSeconds;    // time - ctx.userAwaySince (0 if not currently away)
    bool   validProfile;      // ctx.validProfile
    bool   stateEligible;     // ctx.state in {Continuous, ContinuousStandby}
};

// True iff the proximity reading flipped false -> true on this tick AND the
// user was away for at least kMinAwaySeconds AND the saved profile is valid
// AND we're in a state where a snap makes sense. All four are necessary --
// missing any one is either "not a warm restart" (no edge, or too brief) or
// "no profile to snap to" (one-shot wizard, no calibration yet, etc).
constexpr bool ShouldEngage(const EngageInput& in) {
    return !in.wasPresent
        && in.nowPresent
        && in.awayForSeconds >= kMinAwaySeconds
        && in.validProfile
        && in.stateEligible;
}

}  // namespace spacecal::warm_restart
