#pragma once

// Head-from-tracker rigid-offset solver.
//
// Given a stream of paired (hmd_world, tracker_world) poses collected while
// the user moves their head, solves for T such that:
//
//     hmd_world_i ~= tracker_world_i * T
//
// Uses the dual-quaternion / Kabsch approach: collect relative transforms
// T_i = tracker_world_i^-1 * hmd_world_i, then average their rotation
// components via quaternion averaging and solve the translation by a linear
// least-squares pass on the rotation-unbiased residuals.

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <string>
#include <vector>

namespace wkopenvr::headmount {

enum class SolveState {
    Idle,
    Collecting,   // gathering pose pairs while user moves
    Solving,      // CalibrationCalc running
    Done,         // success; result available
    Failed        // not enough motion, residual too high, etc.
};

struct SolveResult {
    Eigen::AffineCompact3d headFromTracker = Eigen::AffineCompact3d::Identity();
    double residualMm   = 0.0;
    int    samplesUsed  = 0;
    std::string failReason;   // empty on success
};

class Solver {
public:
    // Transition Idle -> Collecting; clears any prior buffer.
    void Start();

    // Abort from any state -> Idle, buffer cleared.
    void Cancel();

    // Transition Collecting -> Solving and run the solve synchronously.
    // Populates m_result; state becomes Done or Failed.
    void Finish();

    // Per-tick feed from the calibration loop.
    // hmdPose and trackerPose must be in the same world coordinate frame.
    // hmdSpeedMps is the HMD linear speed; used to gate on motion.
    // Returns true when the sample was accepted.
    bool Tick(const Eigen::Affine3d& hmdPose,
              const Eigen::Affine3d& trackerPose,
              double hmdSpeedMps);

    SolveState          state()       const { return m_state; }
    const SolveResult&  result()      const { return m_result; }
    size_t              sampleCount() const { return m_pairs.size(); }

    // Minimum fraction of target samples required for Finish to attempt the
    // solve. Exposed so tests can override without needing template tricks.
    static constexpr double kMinHmdSpeedMps      = 0.05;
    static constexpr size_t kTargetSampleCount   = 200;
    static constexpr double kResidualThresholdMm = 5.0;

private:
    SolveState m_state = SolveState::Idle;

    // Each entry: (hmd_world, tracker_world) at the same time-step.
    std::vector<std::pair<Eigen::Affine3d, Eigen::Affine3d>> m_pairs;
    SolveResult m_result;
};

} // namespace wkopenvr::headmount
