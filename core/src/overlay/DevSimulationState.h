#pragma once

#include <atomic>
#include <cstdint>

namespace openvr_pair::overlay::devsim {

constexpr int32_t kHmdId = 0;
constexpr int32_t kTrackerId = 61;
constexpr int32_t kLeftControllerId = 62;
constexpr int32_t kRightControllerId = 63;

inline std::atomic_bool g_enabled{false};
inline std::atomic_bool g_includeIndexControllers{true};

inline const char* ReferenceTrackingSystem()
{
	return "Dev Quest";
}

inline const char* TargetTrackingSystem()
{
	return "lighthouse";
}

inline const char* SerialForDeviceId(int32_t id)
{
	switch (id) {
	case kHmdId: return "WKOPENVR_DEV_FAKE_HMD";
	case kTrackerId: return "WKOPENVR_DEV_FAKE_TRACKER";
	case kLeftControllerId: return "WKOPENVR_DEV_FAKE_INDEX_L";
	case kRightControllerId: return "WKOPENVR_DEV_FAKE_INDEX_R";
	default: return "";
	}
}

inline const char* ModelForDeviceId(int32_t id)
{
	switch (id) {
	case kHmdId: return "Simulated Quest HMD";
	case kTrackerId: return "Simulated Vive Tracker";
	case kLeftControllerId: return "Simulated Index Controller Left";
	case kRightControllerId: return "Simulated Index Controller Right";
	default: return "";
	}
}

inline bool IsEnabled()
{
	return g_enabled.load(std::memory_order_relaxed);
}

inline void SetEnabled(bool enabled)
{
	g_enabled.store(enabled, std::memory_order_relaxed);
}

inline bool IncludeIndexControllers()
{
	return g_includeIndexControllers.load(std::memory_order_relaxed);
}

inline void SetIncludeIndexControllers(bool enabled)
{
	g_includeIndexControllers.store(enabled, std::memory_order_relaxed);
}

inline bool IsFakeDeviceId(int32_t id)
{
	return id == kHmdId
		|| id == kTrackerId
		|| id == kLeftControllerId
		|| id == kRightControllerId;
}

} // namespace openvr_pair::overlay::devsim
