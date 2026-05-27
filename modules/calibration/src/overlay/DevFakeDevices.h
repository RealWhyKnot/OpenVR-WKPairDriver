#pragma once

#include "VRState.h"
#include "DevSimulationState.h"

#include <openvr.h>

#include <cstdint>
#include <string>

struct CalibrationContext;

namespace spacecal::devfake {

constexpr int32_t kHmdId = openvr_pair::overlay::devsim::kHmdId;
constexpr int32_t kTrackerId = openvr_pair::overlay::devsim::kTrackerId;
constexpr int32_t kLeftControllerId = openvr_pair::overlay::devsim::kLeftControllerId;
constexpr int32_t kRightControllerId = openvr_pair::overlay::devsim::kRightControllerId;

const char* ReferenceTrackingSystem();
const char* TargetTrackingSystem();
const char* SerialForDeviceId(int32_t id);
const char* ModelForDeviceId(int32_t id);

bool IsEnabled();
void SetEnabled(bool enabled);

bool IncludeIndexControllers();
void SetIncludeIndexControllers(bool enabled);

bool IsFakeDeviceId(int32_t id);
void ConfigureContext(CalibrationContext& ctx);
void AppendDevices(VRState& state);
void TickPoses(CalibrationContext& ctx, double timeSeconds);

} // namespace spacecal::devfake
