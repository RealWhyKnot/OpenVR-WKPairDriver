#pragma once

// Live eye-position marker overlay.
//
// When the offset modal is open or nudge sliders are being adjusted, a small
// colored dot is rendered in the VR world at the computed eye position so the
// user can verify the offset visually.

#include <Eigen/Geometry>

namespace wkopenvr::headmount {

// Call each frame while the preview should be visible (modal open or sliders
// active). Pass wantVisible=false to destroy the overlay.
//
// headTrackerPose: tracker world pose (Affine3d, world frame)
// headFromTracker: offset transform to compose onto tracker pose
void TickPreview(bool wantVisible,
                 const Eigen::Affine3d& headTrackerPose,
                 const Eigen::AffineCompact3d& headFromTracker);

} // namespace wkopenvr::headmount
