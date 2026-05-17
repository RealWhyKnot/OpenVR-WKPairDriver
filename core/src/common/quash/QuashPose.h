#pragma once

#include <openvr_driver.h>

namespace openvr_pair::common::quash {

// Park position for a hidden tracker: 1 km below the floor.
constexpr double kQuashParkY = -1000.0;

// Force a tracker's pose to a fixed off-screen park position while
// keeping the device connection alive so downstream consumers (VRChat
// etc.) retain their bindings. result=Calibrating_OutOfRange so
// SteamVR dims the entry instead of treating it as live.
inline void ApplyQuashToPose(vr::DriverPose_t& pose)
{
    pose = vr::DriverPose_t{};
    pose.qWorldFromDriverRotation = { 1.0, 0.0, 0.0, 0.0 };
    pose.qDriverFromHeadRotation  = { 1.0, 0.0, 0.0, 0.0 };
    pose.qRotation                = { 1.0, 0.0, 0.0, 0.0 };
    pose.vecPosition[1]           = kQuashParkY;
    pose.deviceIsConnected        = true;
    pose.poseIsValid              = true;
    pose.result                   = vr::TrackingResult_Calibrating_OutOfRange;
}

} // namespace openvr_pair::common::quash
