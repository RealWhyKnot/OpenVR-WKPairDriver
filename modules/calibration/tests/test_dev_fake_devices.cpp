#include "DevFakeDevices.h"
#include "Calibration.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

struct FakeDeviceReset {
	~FakeDeviceReset()
	{
		spacecal::devfake::SetIncludeIndexControllers(true);
		spacecal::devfake::SetEnabled(false);
	}
};

} // namespace

TEST(DevFakeDevices, AppendDevicesAddsTrackerHmdAndControllers)
{
	FakeDeviceReset reset;
	spacecal::devfake::SetEnabled(true);

	VRState state;
	spacecal::devfake::AppendDevices(state);

	EXPECT_NE(state.FindDevice(
		spacecal::devfake::ReferenceTrackingSystem(),
		spacecal::devfake::ModelForDeviceId(spacecal::devfake::kHmdId),
		spacecal::devfake::SerialForDeviceId(spacecal::devfake::kHmdId)), -1);
	EXPECT_NE(state.FindDevice(
		spacecal::devfake::TargetTrackingSystem(),
		spacecal::devfake::ModelForDeviceId(spacecal::devfake::kTrackerId),
		spacecal::devfake::SerialForDeviceId(spacecal::devfake::kTrackerId)), -1);

	const auto left = std::find_if(state.devices.begin(), state.devices.end(), [](const VRDevice& d) {
		return d.id == spacecal::devfake::kLeftControllerId;
	});
	ASSERT_NE(left, state.devices.end());
	EXPECT_EQ(vr::TrackedDeviceClass_Controller, left->deviceClass);
	EXPECT_EQ(vr::TrackedControllerRole_LeftHand, left->controllerRole);
}

TEST(DevFakeDevices, ControllerToggleRemovesIndexControllers)
{
	FakeDeviceReset reset;
	spacecal::devfake::SetEnabled(true);
	spacecal::devfake::SetIncludeIndexControllers(false);

	VRState state;
	spacecal::devfake::AppendDevices(state);

	const auto left = std::find_if(state.devices.begin(), state.devices.end(), [](const VRDevice& d) {
		return d.id == spacecal::devfake::kLeftControllerId;
	});
	EXPECT_EQ(left, state.devices.end());
}

TEST(DevFakeDevices, TickPosesSeedsValidMovingPoses)
{
	FakeDeviceReset reset;
	spacecal::devfake::SetEnabled(true);
	spacecal::devfake::ConfigureContext(CalCtx);

	spacecal::devfake::TickPoses(CalCtx, 1.0);
	const auto firstHmd = CalCtx.devicePoses[spacecal::devfake::kHmdId];
	const auto firstTracker = CalCtx.devicePoses[spacecal::devfake::kTrackerId];
	spacecal::devfake::TickPoses(CalCtx, 2.0);
	const auto secondHmd = CalCtx.devicePoses[spacecal::devfake::kHmdId];

	EXPECT_TRUE(firstHmd.poseIsValid);
	EXPECT_TRUE(firstTracker.poseIsValid);
	EXPECT_EQ(vr::TrackingResult_Running_OK, firstHmd.result);
	EXPECT_EQ(vr::TrackingResult_Running_OK, firstTracker.result);
	EXPECT_NE(firstHmd.vecPosition[0], secondHmd.vecPosition[0]);
	EXPECT_EQ(spacecal::devfake::kHmdId, CalCtx.referenceID);
	EXPECT_EQ(spacecal::devfake::kTrackerId, CalCtx.targetID);
	EXPECT_EQ(spacecal::devfake::kLeftControllerId, CalCtx.controllerIDs[0]);
	EXPECT_EQ(spacecal::devfake::kRightControllerId, CalCtx.controllerIDs[1]);
}
