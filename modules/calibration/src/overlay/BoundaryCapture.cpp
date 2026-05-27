#include "BoundaryCapture.h"
#include "Boundary.h"
#include "CalibrationMetrics.h"

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace wkopenvr::boundary {

namespace {

double DistanceSq(const BoundaryVertex& a, const BoundaryVertex& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

double SegmentDistanceSq(const BoundaryVertex& p,
                         const BoundaryVertex& a,
                         const BoundaryVertex& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double dz = b.z - a.z;
    const double lenSq = dx * dx + dy * dy + dz * dz;
    const double px = p.x - a.x;
    const double py = p.y - a.y;
    const double pz = p.z - a.z;
    if (lenSq < 1e-20) {
        return px * px + py * py + pz * pz;
    }
    double t = (px * dx + py * dy + pz * dz) / lenSq;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double rx = px - t * dx;
    const double ry = py - t * dy;
    const double rz = pz - t * dz;
    return rx * rx + ry * ry + rz * rz;
}

enum class FloorProjectionStatus {
    Ok,
    NonFinite,
    NotDownward,
    DistanceOutOfRange,
};

struct FloorProjectionAttempt {
    FloorProjectionStatus status = FloorProjectionStatus::NonFinite;
    BoundaryVertex hit = {};
    const char* rayName = "-Z";
    double originY = 0.0;
    double aimY = 0.0;
    double distanceMeters = 0.0;
};

const char* ProjectionStatusName(FloorProjectionStatus status) {
    switch (status) {
    case FloorProjectionStatus::Ok: return "ok";
    case FloorProjectionStatus::NonFinite: return "non_finite";
    case FloorProjectionStatus::NotDownward: return "not_downward";
    case FloorProjectionStatus::DistanceOutOfRange: return "distance_out_of_range";
    }
    return "unknown";
}

FloorProjectionAttempt ProjectSingleRayToFloor(
    const Eigen::Affine3d& controllerPose,
    const Eigen::Vector3d& localRay,
    const char* rayName,
    double floorY)
{
    FloorProjectionAttempt attempt;
    attempt.rayName = rayName;

    const Eigen::Vector3d origin = controllerPose.translation();
    const Eigen::Vector3d aim =
        (controllerPose.rotation() * localRay).normalized();
    attempt.originY = origin.y();
    attempt.aimY = aim.y();

    if (!origin.allFinite() || !aim.allFinite()) {
        attempt.status = FloorProjectionStatus::NonFinite;
        return attempt;
    }

    if (aim.y() > -0.20) {
        attempt.status = FloorProjectionStatus::NotDownward;
        return attempt;
    }

    const double distanceMeters = (floorY - origin.y()) / aim.y();
    attempt.distanceMeters = distanceMeters;
    if (!std::isfinite(distanceMeters) ||
        distanceMeters < 0.05 ||
        distanceMeters > 5.0) {
        attempt.status = FloorProjectionStatus::DistanceOutOfRange;
        return attempt;
    }

    const Eigen::Vector3d hit = origin + aim * distanceMeters;
    if (!hit.allFinite()) {
        attempt.status = FloorProjectionStatus::NonFinite;
        return attempt;
    }

    attempt.status = FloorProjectionStatus::Ok;
    attempt.hit = { hit.x(), floorY, hit.z() };
    return attempt;
}

FloorProjectionAttempt ProjectAimToFloor(
    const Eigen::Affine3d& controllerPose,
    double floorY)
{
    // OpenVR controller poses are not guaranteed to expose the model pointer
    // ray on the same local Z sign across controller drivers. Keep -Z first
    // for the historical path, but accept +Z as a narrow fallback.
    const FloorProjectionAttempt primary = ProjectSingleRayToFloor(
        controllerPose,
        Eigen::Vector3d(0.0, 0.0, -1.0),
        "-Z",
        floorY);
    if (primary.status == FloorProjectionStatus::Ok) {
        return primary;
    }

    const FloorProjectionAttempt oppositeZ = ProjectSingleRayToFloor(
        controllerPose,
        Eigen::Vector3d(0.0, 0.0, 1.0),
        "+Z",
        floorY);
    if (oppositeZ.status == FloorProjectionStatus::Ok) {
        return oppositeZ;
    }

    return primary;
}

std::vector<BoundaryVertex> RemoveNearDuplicates(
    const std::vector<BoundaryVertex>& raw,
    double minDistanceMeters)
{
    std::vector<BoundaryVertex> out;
    out.reserve(raw.size());

    const double minDistSq = minDistanceMeters * minDistanceMeters;
    for (const auto& v : raw) {
        if (!out.empty() && DistanceSq(out.back(), v) < minDistSq) {
            continue;
        }
        out.push_back(v);
    }
    return out;
}

std::vector<BoundaryVertex> RemoveClosedCollinearVertices(
    const std::vector<BoundaryVertex>& raw,
    double toleranceMeters)
{
    std::vector<BoundaryVertex> out = raw;
    const double toleranceSq = toleranceMeters * toleranceMeters;

    bool changed = true;
    while (changed && out.size() > 3) {
        changed = false;
        for (size_t i = 0; i < out.size(); ++i) {
            const BoundaryVertex& prev = out[(i + out.size() - 1) % out.size()];
            const BoundaryVertex& cur = out[i];
            const BoundaryVertex& next = out[(i + 1) % out.size()];
            if (SegmentDistanceSq(cur, prev, next) <= toleranceSq) {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }

    return out;
}

std::vector<BoundaryVertex> CleanPaintedLoop(
    const std::vector<BoundaryVertex>& raw,
    double debounceMeters,
    double closeLoopMeters,
    double simplifyMeters)
{
    std::vector<BoundaryVertex> path = RemoveNearDuplicates(raw, debounceMeters);
    if (path.size() < 3) {
        return path;
    }

    const double closeSq = closeLoopMeters * closeLoopMeters;
    while (path.size() >= 3 && DistanceSq(path.front(), path.back()) < closeSq) {
        path.pop_back();
    }

    if (path.size() < 3) {
        return path;
    }

    auto kept = SimplifyDouglasPeucker(path, simplifyMeters);
    std::vector<BoundaryVertex> simplified;
    simplified.reserve(kept.size());
    for (size_t idx : kept) {
        simplified.push_back(path[idx]);
    }

    simplified = RemoveNearDuplicates(simplified, debounceMeters);
    if (simplified.size() >= 3 &&
        DistanceSq(simplified.front(), simplified.back()) < closeSq) {
        simplified.pop_back();
    }
    return RemoveClosedCollinearVertices(simplified, simplifyMeters);
}

}  // namespace

void CaptureSession::Start() {
    m_raw.clear();
    m_simplified.clear();
    m_projectionRejectLogCount = 0;
    ++m_sessionId;
    m_state = CaptureState::Active;
    Metrics::WriteLogAnnotation("[boundary-capture] started");
}

void CaptureSession::Cancel() {
    m_raw.clear();
    m_simplified.clear();
    m_state = CaptureState::Idle;
    Metrics::WriteLogAnnotation("[boundary-capture] cancelled");
}

void CaptureSession::Finish() {
    if (m_state != CaptureState::Active) return;
    m_simplified = CleanPaintedLoop(
        m_raw,
        kVertexDebounceMeters,
        kCloseLoopMeters,
        kSimplifyEpsilonMeters);
    m_state = CaptureState::Finished;
    {
        char lbuf[96];
        snprintf(lbuf, sizeof lbuf,
            "[boundary-capture] finished: raw=%zu simplified=%zu",
            m_raw.size(), m_simplified.size());
        Metrics::WriteLogAnnotation(lbuf);
    }
}

bool CaptureSession::Tick(const Eigen::Affine3d& controllerPose,
                          bool triggerHeld,
                          double floorY) {
    if (m_state != CaptureState::Active) return false;
    if (!triggerHeld) return false;

    const FloorProjectionAttempt projection = ProjectAimToFloor(controllerPose, floorY);
    if (projection.status != FloorProjectionStatus::Ok) {
        ++m_projectionRejectLogCount;
        if (m_projectionRejectLogCount == 1 || (m_projectionRejectLogCount % 120) == 0) {
            char lbuf[224];
            snprintf(lbuf, sizeof lbuf,
                "[boundary-capture] floor projection rejected: reason=%s ray=%s origin_y=%.3f aim_y=%.3f distance=%.3f rejects=%zu",
                ProjectionStatusName(projection.status),
                projection.rayName,
                projection.originY,
                projection.aimY,
                projection.distanceMeters,
                m_projectionRejectLogCount);
            Metrics::WriteLogAnnotation(lbuf);
        }
        return false;
    }

    const BoundaryVertex& candidate = projection.hit;
    if (!m_raw.empty()) {
        const BoundaryVertex& last = m_raw.back();
        const double dist = std::sqrt(DistanceSq(candidate, last));
        if (dist < kVertexDebounceMeters) return false;
    }

    if (m_raw.empty()) {
        char lbuf[128];
        snprintf(lbuf, sizeof lbuf,
            "[boundary-capture] first vertex: pos=(%.3f,%.3f,%.3f) ray=%s",
            candidate.x, candidate.y, candidate.z, projection.rayName);
        Metrics::WriteLogAnnotation(lbuf);
    }
    m_raw.push_back(candidate);
    return true;
}

const std::vector<BoundaryVertex>& CaptureSession::vertices() const {
    if (m_state == CaptureState::Finished) return m_simplified;
    return m_raw;
}

}  // namespace wkopenvr::boundary
