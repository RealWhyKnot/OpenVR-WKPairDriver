#pragma once

#include "Calibration.h"

#include <cstdint>
#include <string>

namespace wkopenvr::headmount {

inline bool ShouldHideHeadMountTracker(const CalibrationContext& ctx,
	uint32_t openVrId,
	const std::string& serial,
	const std::string& trackingSystem)
{
	if (openVrId == vr::k_unTrackedDeviceIndex_Hmd) return false;

	const HeadMountConfig& hm = ctx.headMount;
	if (!hm.hideTracker) return false;
	if (hm.trackerSerial.empty() || serial.empty()) return false;
	if (serial != hm.trackerSerial) return false;
	if (!hm.trackerTrackingSystem.empty()
		&& !trackingSystem.empty()
		&& trackingSystem != hm.trackerTrackingSystem) {
		return false;
	}

	return true;
}

inline bool ShouldHideContinuousTarget(const CalibrationContext& ctx,
	uint32_t openVrId)
{
	if (openVrId == vr::k_unTrackedDeviceIndex_Hmd) return false;
	return ctx.state == CalibrationState::Continuous
		&& static_cast<int32_t>(openVrId) == ctx.targetID
		&& ctx.quashTargetInContinuous;
}

inline bool ShouldQuashPublishedTrackerPose(const CalibrationContext& ctx,
	uint32_t openVrId,
	const std::string& serial,
	const std::string& trackingSystem)
{
	return ShouldHideHeadMountTracker(ctx, openVrId, serial, trackingSystem)
		|| ShouldHideContinuousTarget(ctx, openVrId);
}

// The vertical floor offset shifts all calibrated content in world-space Y. That
// is only coherent when the rendered headset is also being repositioned, i.e.
// DriverSynth. With synth off the real headset owns its pose, so applying the
// offset would slide calibrated content out of alignment. Gate the applied value
// here; the stored offset is left untouched so it returns when synth is
// re-enabled.
inline double EffectiveFloorOffsetMetersY(HeadMountMode mode, double storedOffsetMetersY)
{
	return mode == HeadMountMode::DriverSynth ? storedOffsetMetersY : 0.0;
}

} // namespace wkopenvr::headmount
