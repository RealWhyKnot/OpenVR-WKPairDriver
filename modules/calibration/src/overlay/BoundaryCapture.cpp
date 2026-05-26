#include "BoundaryCapture.h"
#include "Boundary.h"
#include "CalibrationMetrics.h"

#include <cmath>
#include <cstdio>

namespace wkopenvr::boundary {

void CaptureSession::Start() {
    m_raw.clear();
    m_simplified.clear();
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
    if (m_raw.size() >= 2) {
        auto kept = SimplifyDouglasPeucker(m_raw, kSimplifyEpsilonMeters);
        m_simplified.clear();
        m_simplified.reserve(kept.size());
        for (size_t idx : kept) {
            m_simplified.push_back(m_raw[idx]);
        }
    } else {
        m_simplified = m_raw;
    }
    m_state = CaptureState::Finished;
    {
        char lbuf[96];
        snprintf(lbuf, sizeof lbuf,
            "[boundary-capture] finished: raw=%zu simplified=%zu",
            m_raw.size(), m_simplified.size());
        Metrics::WriteLogAnnotation(lbuf);
    }
}

void CaptureSession::Tick(const Eigen::Affine3d& controllerPose, bool triggerHeld) {
    if (m_state != CaptureState::Active) return;
    if (!triggerHeld) return;

    const Eigen::Vector3d pos = controllerPose.translation();
    const BoundaryVertex candidate{ pos.x(), pos.y(), pos.z() };

    if (!m_raw.empty()) {
        const BoundaryVertex& last = m_raw.back();
        const double dx = candidate.x - last.x;
        const double dy = candidate.y - last.y;
        const double dz = candidate.z - last.z;
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < kVertexDebounceMeters) return;
    }

    if (m_raw.empty()) {
        char lbuf[128];
        snprintf(lbuf, sizeof lbuf,
            "[boundary-capture] first vertex: pos=(%.3f,%.3f,%.3f)",
            candidate.x, candidate.y, candidate.z);
        Metrics::WriteLogAnnotation(lbuf);
    }
    m_raw.push_back(candidate);
}

const std::vector<BoundaryVertex>& CaptureSession::vertices() const {
    if (m_state == CaptureState::Finished) return m_simplified;
    return m_raw;
}

}  // namespace wkopenvr::boundary
