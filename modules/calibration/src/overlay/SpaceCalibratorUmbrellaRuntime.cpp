#include "SpaceCalibratorUmbrellaRuntime.h"

#include "Calibration.h"
#include "CalibrationAnchor.h"
#include "CalibrationMetrics.h"
#include "Configuration.h"
#include "DevFakeDevices.h"
#include "GuardianAutoApply.h"
#include "UserInterface.h"

#include "AdbController.h"

#include <openvr.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

// Singleton AdbController for the calibration plugin. Constructed once;
// all Guardian auto-apply calls go through this instance so the binary
// path is resolved only once.
AdbController g_adb;

bool g_profileLoaded = false;
bool g_vrReady = false;
std::string g_lastVRError;
std::chrono::steady_clock::time_point g_lastRetry{};
const auto g_retryPeriod = std::chrono::seconds(1);

double SecondsSinceStart()
{
	static const auto start = std::chrono::steady_clock::now();
	const auto now = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(now - start).count();
}

bool TryConnect()
{
	if (g_vrReady) return true;

	if (!vr::VRSystem()) {
		g_lastVRError = "Waiting for SteamVR";
		return false;
	}

	if (!vr::VR_IsInterfaceVersionValid(vr::IVRSystem_Version) ||
		!vr::VR_IsInterfaceVersionValid(vr::IVRSettings_Version)) {
		g_lastVRError = "OpenVR interface version mismatch";
		return false;
	}

	try {
		InitCalibrator();
	} catch (const std::exception &e) {
		g_lastVRError = e.what();
		return false;
	}

	g_lastVRError.clear();
	g_vrReady = true;
	Metrics::WriteLogAnnotation("[umbrella] vr_ready");
	return true;
}

std::string ReadDeviceSerial(int32_t id)
{
	if (id < 0 || id >= (int32_t)vr::k_unMaxTrackedDeviceCount) return {};
	auto *vrSystem = vr::VRSystem();
	if (!vrSystem) return {};

	char buf[vr::k_unMaxPropertyStringSize] = {};
	vr::ETrackedPropertyError err = vr::TrackedProp_Success;
	vrSystem->GetStringTrackedDeviceProperty(
		(uint32_t)id,
		vr::Prop_SerialNumber_String,
		buf, sizeof buf, &err);
	if (err != vr::TrackedProp_Success || buf[0] == '\0') return {};
	return buf;
}

void AddCalibrationLock(std::vector<openvr_pair::overlay::CalibrationDeviceLock> &locks,
	openvr_pair::overlay::CalibrationDeviceLockKind kind,
	int32_t liveId,
	const std::string &fallbackSerial)
{
	std::string serial = ReadDeviceSerial(liveId);
	if (serial.empty()) serial = fallbackSerial;
	if (serial.empty()) return;

	for (const auto &existing : locks) {
		if (existing.serial == serial) return;
	}
	locks.push_back({ serial, kind });
}

} // namespace

void CCal_UmbrellaStart()
{
	if (!g_profileLoaded) {
		LoadProfile(CalCtx);
		g_profileLoaded = true;
		// Profile load is complete; run the gated Guardian pause sequence
		// before the render loop starts so the headset is in the correct
		// state by the time the first frame renders.
		wkopenvr::adb::MaybeAutoApplyAtStart(g_adb);
	}
	g_lastRetry = std::chrono::steady_clock::now() - g_retryPeriod;
}

void CCal_UmbrellaTick()
{
	const auto now = std::chrono::steady_clock::now();
	if (!g_vrReady && now - g_lastRetry >= g_retryPeriod) {
		g_lastRetry = now;
		if (!spacecal::devfake::IsEnabled()) {
			TryConnect();
		}
	}

	// Low-rate ADB health poll. TickGuardianHealth enforces its own 7 s cadence
	// internally so calling it every tick is cheap when ADB is idle.
	wkopenvr::adb::TickGuardianHealth(g_adb);

	if (g_vrReady || spacecal::devfake::IsEnabled()) {
		CalibrationTick(SecondsSinceStart());

		std::vector<openvr_pair::overlay::CalibrationDeviceLock> locks;
		const bool continuous =
			CalCtx.state == CalibrationState::Continuous ||
			CalCtx.state == CalibrationState::ContinuousStandby;
		if (continuous) {
			AddCalibrationLock(locks,
				openvr_pair::overlay::CalibrationDeviceLockKind::Reference,
				CalCtx.referenceID, CalCtx.referenceStandby.serial);
			AddCalibrationLock(locks,
				openvr_pair::overlay::CalibrationDeviceLockKind::Target,
				CalCtx.targetID, CalCtx.targetStandby.serial);
			for (const auto &extra : CalCtx.additionalCalibrations) {
				if (!extra.enabled) continue;
				AddCalibrationLock(locks,
					openvr_pair::overlay::CalibrationDeviceLockKind::Target,
					extra.targetID, extra.targetStandby.serial);
			}
		}
		openvr_pair::overlay::SetCalibrationDeviceLocks(locks);
	} else {
		static auto s_lastWaitingLog = std::chrono::steady_clock::time_point{};
		if (s_lastWaitingLog.time_since_epoch().count() == 0 ||
			now - s_lastWaitingLog >= std::chrono::seconds(5)) {
			s_lastWaitingLog = now;
			char buf[192];
			snprintf(buf, sizeof buf,
				"[umbrella] calibration_tick_skipped reason=vr_not_ready detail='%s'",
				g_lastVRError.empty() ? "unknown" : g_lastVRError.c_str());
			Metrics::WriteLogAnnotation(buf);
		}
		openvr_pair::overlay::SetCalibrationDeviceLocks({});
	}
}

void CCal_UmbrellaShutdown()
{
	g_vrReady = false;
}

AdbController& CCal_GetAdb()
{
	return g_adb;
}

void RequestImmediateRedraw()
{
}

void RequestExit()
{
}

bool IsVRReady()
{
	return g_vrReady || spacecal::devfake::IsEnabled();
}

const std::string &LastVRConnectError()
{
	return g_lastVRError;
}
