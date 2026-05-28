#pragma once

#include "Calibration.h"

#include <Eigen/Geometry>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wkopenvr::boundary {

enum class CaptureState { Idle, Active, Finished };

struct FloorHitPreview {
    bool valid = false;
    BoundaryVertex hit = {};
    const char* rayName = "";
};

// Streams controller aim rays while the trigger is held and builds a boundary
// polygon in lighthouse (pre-transform) space. Call Finish() to clean the raw
// painted loop down to edge vertices. Cancel() discards the buffer and resets
// to Idle.
class CaptureSession {
public:
    void Start();
    void Cancel();
    void Finish();

    // Called each overlay tick. controllerPose is the raw lighthouse-space
    // pose. The controller's pointer ray is intersected with the floor plane;
    // a new vertex is appended when triggerHeld AND that floor point has moved
    // at least kVertexDebounceMeters from the last recorded position.
    bool Tick(const Eigen::Affine3d& controllerPose, bool triggerHeld, double floorY = 0.0);
    bool TickPointerPose(const Eigen::Affine3d& pointerPose, bool triggerHeld, double floorY = 0.0);
    bool TickProjectedPosition(const Eigen::Affine3d& controllerPose, bool active, double floorY = 0.0);

    FloorHitPreview PreviewControllerFloorHit(
        const Eigen::Affine3d& controllerPose,
        double floorY = 0.0) const;
    FloorHitPreview PreviewPointerFloorHit(
        const Eigen::Affine3d& pointerPose,
        double floorY = 0.0) const;

    CaptureState state() const { return m_state; }
    uint64_t sessionId() const { return m_sessionId; }
    // Simplified vertices after Finish(); raw vertices while Active.
    const std::vector<BoundaryVertex>& vertices() const;
    size_t rawVertexCount() const { return m_raw.size(); }

private:
    CaptureState m_state = CaptureState::Idle;
    uint64_t m_sessionId = 0;
    size_t m_projectionRejectLogCount = 0;
    size_t m_debounceRejectLogCount = 0;
    std::vector<BoundaryVertex> m_raw;
    std::vector<BoundaryVertex> m_simplified;

    bool AppendProjection(
        const Eigen::Affine3d& poseForLog,
        bool triggerHeld,
        double floorY,
        bool pointerOnly);
    bool AppendProjectedPosition(
        const Eigen::Affine3d& controllerPose,
        bool active,
        double floorY);

    // Minimum motion required to record a new vertex (avoids duplicate points
    // while the user holds still with the trigger pressed).
    static constexpr double kVertexDebounceMeters = 0.05;
    // Perpendicular-distance tolerance for Douglas-Peucker.
    static constexpr double kSimplifyEpsilonMeters = 0.05;
    static constexpr double kCloseLoopMeters = 0.15;
};

}  // namespace wkopenvr::boundary
