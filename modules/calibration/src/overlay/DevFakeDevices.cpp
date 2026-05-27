#include "DevFakeDevices.h"

#include "Calibration.h"
#include "CalibrationMetrics.h"

#include <Eigen/Dense>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace spacecal::devfake {
namespace {

bool g_configuredSelection = false;

namespace devsim = openvr_pair::overlay::devsim;

void PushUniqueSystem(VRState& state, const std::string& system)
{
	if (std::find(state.trackingSystems.begin(), state.trackingSystems.end(), system)
		== state.trackingSystems.end()) {
		state.trackingSystems.push_back(system);
	}
}

void PushDevice(VRState& state, int id, vr::TrackedDeviceClass deviceClass,
	const char* model, const char* serial, const char* trackingSystem,
	vr::ETrackedControllerRole role = vr::TrackedControllerRole_Invalid)
{
	auto exists = std::find_if(state.devices.begin(), state.devices.end(),
		[id](const VRDevice& device) { return device.id == id; });
	VRDevice device{};
	device.id = id;
	device.deviceClass = deviceClass;
	device.model = model ? model : "";
	device.serial = serial ? serial : "";
	device.trackingSystem = trackingSystem ? trackingSystem : "";
	device.controllerRole = role;
	if (exists != state.devices.end()) {
		*exists = device;
		return;
	}
	state.devices.push_back(device);
}

Eigen::Quaterniond RotationFromEuler(double yaw, double pitch, double roll)
{
	Eigen::Quaterniond q =
		Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitY())
		* Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitX())
		* Eigen::AngleAxisd(roll, Eigen::Vector3d::UnitZ());
	q.normalize();
	return q;
}

vr::DriverPose_t MakePose(
	const Eigen::Vector3d& position,
	const Eigen::Quaterniond& rotation,
	const Eigen::Vector3d& velocity = Eigen::Vector3d::Zero())
{
	vr::DriverPose_t pose{};
	pose.deviceIsConnected = true;
	pose.poseIsValid = true;
	pose.result = vr::TrackingResult_Running_OK;
	pose.qWorldFromDriverRotation.w = 1.0;
	pose.qDriverFromHeadRotation.w = 1.0;
	pose.qRotation.w = rotation.w();
	pose.qRotation.x = rotation.x();
	pose.qRotation.y = rotation.y();
	pose.qRotation.z = rotation.z();
	pose.vecPosition[0] = position.x();
	pose.vecPosition[1] = position.y();
	pose.vecPosition[2] = position.z();
	pose.vecVelocity[0] = velocity.x();
	pose.vecVelocity[1] = velocity.y();
	pose.vecVelocity[2] = velocity.z();
	return pose;
}

Eigen::Affine3d TargetFromReference()
{
	const Eigen::Quaterniond q = RotationFromEuler(
		15.0 * EIGEN_PI / 180.0,
		-6.0 * EIGEN_PI / 180.0,
		4.0 * EIGEN_PI / 180.0);
	return Eigen::Translation3d(0.42, 0.08, -0.25) * q;
}

Eigen::Affine3d ReferencePose(double t)
{
	const Eigen::Vector3d pos(
		0.32 * std::sin(t * 0.73),
		1.55 + 0.10 * std::sin(t * 1.11),
		-0.25 + 0.28 * std::cos(t * 0.61));
	const Eigen::Quaterniond q = RotationFromEuler(
		0.95 * std::sin(t * 0.47),
		0.72 * std::sin(t * 0.79 + 0.4),
		0.62 * std::cos(t * 0.53));
	return Eigen::Translation3d(pos) * q;
}

Eigen::Vector3d ReferenceVelocity(double t)
{
	return Eigen::Vector3d(
		0.32 * 0.73 * std::cos(t * 0.73),
		0.10 * 1.11 * std::cos(t * 1.11),
		-0.28 * 0.61 * std::sin(t * 0.61));
}

void SetPose(CalibrationContext& ctx, int32_t id, const vr::DriverPose_t& pose)
{
	if (id < 0 || id >= (int32_t)vr::k_unMaxTrackedDeviceCount) return;
	ctx.devicePoses[id] = pose;
	QueryPerformanceCounter(&ctx.devicePoseSampleTimes[id]);
}

void ApplySelection(CalibrationContext& ctx)
{
	ctx.referenceTrackingSystem = ReferenceTrackingSystem();
	ctx.referenceID = kHmdId;
	ctx.referenceStandby.trackingSystem = ReferenceTrackingSystem();
	ctx.referenceStandby.model = ModelForDeviceId(kHmdId);
	ctx.referenceStandby.serial = SerialForDeviceId(kHmdId);

	ctx.targetTrackingSystem = TargetTrackingSystem();
	ctx.targetID = kTrackerId;
	ctx.targetStandby.trackingSystem = TargetTrackingSystem();
	ctx.targetStandby.model = ModelForDeviceId(kTrackerId);
	ctx.targetStandby.serial = SerialForDeviceId(kTrackerId);

	for (int32_t& id : ctx.controllerIDs) id = -1;
	if (IncludeIndexControllers() && ctx.MAX_CONTROLLERS >= 2) {
		ctx.controllerIDs[0] = kLeftControllerId;
		ctx.controllerIDs[1] = kRightControllerId;
	}
}

} // namespace

const char* ReferenceTrackingSystem()
{
	return devsim::ReferenceTrackingSystem();
}

const char* TargetTrackingSystem()
{
	return devsim::TargetTrackingSystem();
}

const char* SerialForDeviceId(int32_t id)
{
	return devsim::SerialForDeviceId(id);
}

const char* ModelForDeviceId(int32_t id)
{
	return devsim::ModelForDeviceId(id);
}

bool IsEnabled()
{
	return devsim::IsEnabled();
}

void SetEnabled(bool enabled)
{
	if (IsEnabled() == enabled) return;
	devsim::SetEnabled(enabled);
	g_configuredSelection = false;
	Metrics::WriteLogAnnotation(enabled
		? "[dev-fake-devices] enabled"
		: "[dev-fake-devices] disabled");
	if (enabled) {
		ConfigureContext(CalCtx);
	} else {
		if (IsFakeDeviceId(CalCtx.referenceID)) CalCtx.referenceID = -1;
		if (IsFakeDeviceId(CalCtx.targetID)) CalCtx.targetID = -1;
		for (int32_t& id : CalCtx.controllerIDs) {
			if (IsFakeDeviceId(id)) id = -1;
		}
	}
}

bool IncludeIndexControllers()
{
	return devsim::IncludeIndexControllers();
}

void SetIncludeIndexControllers(bool enabled)
{
	devsim::SetIncludeIndexControllers(enabled);
	if (IsEnabled()) ConfigureContext(CalCtx);
}

bool IsFakeDeviceId(int32_t id)
{
	return devsim::IsFakeDeviceId(id);
}

void ConfigureContext(CalibrationContext& ctx)
{
	if (!IsEnabled()) return;
	ApplySelection(ctx);
	g_configuredSelection = true;
}

void AppendDevices(VRState& state)
{
	if (!IsEnabled()) return;

	PushUniqueSystem(state, ReferenceTrackingSystem());
	PushUniqueSystem(state, TargetTrackingSystem());
	PushDevice(state, kHmdId, vr::TrackedDeviceClass_HMD,
		ModelForDeviceId(kHmdId), SerialForDeviceId(kHmdId), ReferenceTrackingSystem());
	PushDevice(state, kTrackerId, vr::TrackedDeviceClass_GenericTracker,
		ModelForDeviceId(kTrackerId), SerialForDeviceId(kTrackerId), TargetTrackingSystem());

	if (IncludeIndexControllers()) {
		PushDevice(state, kLeftControllerId, vr::TrackedDeviceClass_Controller,
			ModelForDeviceId(kLeftControllerId), SerialForDeviceId(kLeftControllerId), TargetTrackingSystem(),
			vr::TrackedControllerRole_LeftHand);
		PushDevice(state, kRightControllerId, vr::TrackedDeviceClass_Controller,
			ModelForDeviceId(kRightControllerId), SerialForDeviceId(kRightControllerId), TargetTrackingSystem(),
			vr::TrackedControllerRole_RightHand);
	}
}

void TickPoses(CalibrationContext& ctx, double timeSeconds)
{
	if (!IsEnabled()) return;
	if (!g_configuredSelection) ConfigureContext(ctx);

	const Eigen::Affine3d ref = ReferencePose(timeSeconds);
	const Eigen::Affine3d target = TargetFromReference() * ref;
	const Eigen::Vector3d refVelocity = ReferenceVelocity(timeSeconds);
	const Eigen::Vector3d targetVelocity = TargetFromReference().linear() * refVelocity;

	SetPose(ctx, kHmdId, MakePose(
		ref.translation(),
		Eigen::Quaterniond(ref.linear()).normalized(),
		refVelocity));
	SetPose(ctx, kTrackerId, MakePose(
		target.translation(),
		Eigen::Quaterniond(target.linear()).normalized(),
		targetVelocity));

	if (!IncludeIndexControllers()) return;

	const Eigen::Affine3d leftHand =
		target * Eigen::Translation3d(-0.22, -0.28, -0.18)
		* RotationFromEuler(0.0, 0.0, -0.35);
	const Eigen::Affine3d rightHand =
		target * Eigen::Translation3d(0.22, -0.28, -0.18)
		* RotationFromEuler(0.0, 0.0, 0.35);
	SetPose(ctx, kLeftControllerId, MakePose(
		leftHand.translation(),
		Eigen::Quaterniond(leftHand.linear()).normalized(),
		targetVelocity));
	SetPose(ctx, kRightControllerId, MakePose(
		rightHand.translation(),
		Eigen::Quaterniond(rightHand.linear()).normalized(),
		targetVelocity));
}

} // namespace spacecal::devfake
