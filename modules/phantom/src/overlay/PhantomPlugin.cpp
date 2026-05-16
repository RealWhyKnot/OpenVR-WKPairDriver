// openvr.h must precede anything that may pull in openvr_driver.h via the
// IPCClient -> Protocol include chain, matching the smoothing plugin's
// convention. Defining _OPENVR_API early makes Protocol.h skip the driver
// header so we don't redefine vr:: symbols.
#include <openvr.h>

#include "PhantomPlugin.h"

#include "DeviceFilters.h"
#include "Protocol.h"
#include "ShellContext.h"
#include "UiHelpers.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

void PhantomPlugin::OnStart(openvr_pair::overlay::ShellContext&)
{
    (void)ConnectIfNeeded();
    SendConfig();
    // Push the persisted per-device opt-in map so the driver picks up the
    // user's prior choices without needing to wait for them to re-toggle.
    for (const auto& kv : cfg_.dropout_enabled) {
        if (kv.second) SendDeviceOptIn(kv.first, true);
    }
    seededDriver_ = ipc_.IsConnected();
}

void PhantomPlugin::Tick(openvr_pair::overlay::ShellContext&)
{
    if (!ipc_.IsConnected() && ConnectIfNeeded()) {
        // Reconnected after a drop -- replay full state so the driver is in
        // sync with what the overlay believes.
        SendConfig();
        for (const auto& kv : cfg_.dropout_enabled) {
            if (kv.second) SendDeviceOptIn(kv.first, true);
        }
        seededDriver_ = true;
    }

    if (!stateShmemReady_) {
        if (stateShmem_.Open(OPENVR_PAIRDRIVER_PHANTOM_STATE_SHMEM_NAME)) {
            stateShmemReady_ = true;
        }
    }
}

bool PhantomPlugin::ConnectIfNeeded()
{
    if (ipc_.IsConnected()) return false;
    try {
        ipc_.Connect();
        connectError_.clear();
        return true;
    } catch (const std::exception& e) {
        connectError_ = e.what();
    }
    return false;
}

void PhantomPlugin::SendConfig()
{
    if (!ipc_.IsConnected()) return;
    protocol::Request req(protocol::RequestSetPhantomConfig);
    auto& c = req.setPhantomConfig;
    c.master_enabled = cfg_.master_enabled ? 1u : 0u;
    c._pad0[0] = c._pad0[1] = c._pad0[2] = 0;
    c.blend_out_ms   = cfg_.blend_out_ms;
    c.blend_in_ms    = cfg_.blend_in_ms;
    c.reckon_hold_ms = cfg_.reckon_hold_ms;
    c.synth_hold_ms  = cfg_.synth_hold_ms;
    c.lost_hold_ms   = cfg_.lost_hold_ms;
    try {
        ipc_.SendBlocking(req);
    } catch (const std::exception& e) {
        connectError_ = e.what();
    }
}

void PhantomPlugin::SendDeviceOptIn(const std::string& serial, bool enabled)
{
    if (!ipc_.IsConnected()) return;
    // FNV-1a 64-bit of the serial string; matches the driver-side hash so
    // the slot lookup lands on the right device.
    uint64_t h = 0xcbf29ce484222325ull;
    for (char c : serial) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    protocol::Request req(protocol::RequestSetPhantomDeviceOptIn);
    req.setPhantomDeviceOptIn.device_serial_hash = h;
    req.setPhantomDeviceOptIn.dropout_enabled    = enabled ? 1u : 0u;
    std::memset(req.setPhantomDeviceOptIn._reserved, 0,
                sizeof(req.setPhantomDeviceOptIn._reserved));
    try {
        ipc_.SendBlocking(req);
    } catch (const std::exception& e) {
        connectError_ = e.what();
    }
}

void PhantomPlugin::DrawTab(openvr_pair::overlay::ShellContext&)
{
    if (ImGui::BeginTabBar("PhantomTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Dropouts")) {
            DrawDropoutsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics")) {
            DrawDiagnosticsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced")) {
            DrawAdvancedTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (!connectError_.empty() && !ipc_.IsConnected()) {
        ImGui::Spacing();
        ImGui::TextColored(openvr_pair::overlay::ui::GetPalette().statusError,
            "IPC: %s", connectError_.c_str());
    }
}

void PhantomPlugin::DrawDropoutsTab()
{
    ImGui::Spacing();
    if (ImGui::Checkbox("Bridge dropped trackers", &cfg_.master_enabled)) {
        SendConfig();
        SavePhantomConfig(cfg_);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Master switch. With this on, the driver fills in plausible poses\n"
            "for any tracker you opted in below when its real pose goes silent.\n"
            "Past the synth-hold window the tracker is marked OutOfRange so\n"
            "VRChat / Resonite drop it from the IK chain cleanly.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Per-tracker opt-in");
    ImGui::Spacing();

    auto* vrSystem = vr::VRSystem();
    if (!vrSystem) {
        ImGui::TextDisabled("(VR system not available)");
        return;
    }

    char buffer[vr::k_unMaxPropertyStringSize];
    bool anyShown = false;
    for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
        const auto deviceClass = vrSystem->GetTrackedDeviceClass(id);
        if (deviceClass == vr::TrackedDeviceClass_Invalid) continue;

        vr::ETrackedPropertyError err = vr::TrackedProp_Success;
        vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String,
            buffer, sizeof(buffer), &err);
        if (err != vr::TrackedProp_Success || buffer[0] == 0) continue;
        const std::string serial = buffer;

        vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_RenderModelName_String,
            buffer, sizeof(buffer), &err);
        const std::string model = (err == vr::TrackedProp_Success) ? buffer : "";

        if (!openvr_pair::overlay::ShouldShowInSmoothingPredictionList(
                deviceClass, serial, model)) {
            continue;
        }

        anyShown = true;
        bool enabled = cfg_.dropout_enabled.count(serial) ? cfg_.dropout_enabled[serial] : false;
        ImGui::PushID(("trk_" + serial).c_str());
        if (ImGui::Checkbox("##en", &enabled)) {
            cfg_.dropout_enabled[serial] = enabled;
            SendDeviceOptIn(serial, enabled);
            SavePhantomConfig(cfg_);
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s  [%s]",
            model.empty() ? "(unknown model)" : model.c_str(),
            serial.c_str());
        ImGui::PopID();
    }
    if (!anyShown) {
        ImGui::TextDisabled("No bridgeable trackers detected. HMD, controllers, and "
                            "generic body trackers will appear here once SteamVR is "
                            "running and the devices are on.");
    }
}

void PhantomPlugin::DrawDiagnosticsTab()
{
    ImGui::Spacing();
    if (!stateShmemReady_ || !stateShmem_.layout()) {
        ImGui::TextDisabled("Driver state not yet available. The driver must be loaded "
                            "and the phantom feature flag enabled.");
        return;
    }
    const auto* layout = stateShmem_.layout();
    if (layout->magic != phantom::kPhantomStateShmemMagic) {
        ImGui::TextDisabled("Driver state shmem has unexpected magic; mismatched install?");
        return;
    }

    if (ImGui::BeginTable("PhantomDiag", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Serial");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Drops");
        ImGui::TableSetupColumn("Now (ms)");
        ImGui::TableSetupColumn("Longest (ms)");
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < layout->device_count; ++i) {
            const auto& d = layout->devices[i];
            if (d.serial_len == 0) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%.*s", (int)d.serial_len, d.serial);
            ImGui::TableNextColumn();
            ImGui::Text("%s", phantom::TrackerStateLabel(
                static_cast<phantom::TrackerState>(d.state)));
            ImGui::TableNextColumn();
            ImGui::Text("%u", d.dropout_count);
            ImGui::TableNextColumn();
            ImGui::Text("%u", d.dropout_age_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%u", d.longest_dropout_ms);
        }
        ImGui::EndTable();
    }
}

void PhantomPlugin::DrawAdvancedTab()
{
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Timing ladder. Lower the synth-hold to recover trackers faster after "
        "a stuck pose; raise it to bridge longer outages. Out-of-range happens "
        "at synth-hold; the device stops publishing at lost-hold.");
    ImGui::Spacing();

    auto sliderMs = [&](const char* label, uint32_t& v, uint32_t lo, uint32_t hi,
                        const char* tip) {
        int tmp = static_cast<int>(v);
        if (ImGui::SliderInt(label, &tmp, (int)lo, (int)hi, "%d ms")) {
            v = static_cast<uint32_t>(std::clamp(tmp, (int)lo, (int)hi));
            SendConfig();
            SavePhantomConfig(cfg_);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };

    sliderMs("Blend out",  cfg_.blend_out_ms,  0, 500,
        "Real to synth fade duration on dropout start.");
    sliderMs("Blend in",   cfg_.blend_in_ms,   0, 1000,
        "Synth to real fade duration when the real signal returns.");
    sliderMs("Reckon hold", cfg_.reckon_hold_ms, 0, 1000,
        "How long dead reckoning is the primary synthesis source before the "
        "ladder escalates (to IK / ML in later phases).");
    sliderMs("Synth hold", cfg_.synth_hold_ms, 100, 10000,
        "Total time after dropout before ETrackingResult flips to OutOfRange.");
    sliderMs("Lost hold",  cfg_.lost_hold_ms,  500, 60000,
        "Total time after dropout before the device stops publishing entirely.");

    ImGui::Spacing();
    if (ImGui::Button("Reset to defaults")) {
        cfg_.blend_out_ms   = phantom::DefaultTimings::kBlendOutMs;
        cfg_.blend_in_ms    = phantom::DefaultTimings::kBlendInMs;
        cfg_.reckon_hold_ms = phantom::DefaultTimings::kReckonHoldMs;
        cfg_.synth_hold_ms  = phantom::DefaultTimings::kSynthHoldMs;
        cfg_.lost_hold_ms   = phantom::DefaultTimings::kLostHoldMs;
        SendConfig();
        SavePhantomConfig(cfg_);
    }
}

namespace openvr_pair::overlay {

std::unique_ptr<FeaturePlugin> CreatePhantomPlugin()
{
    return std::make_unique<PhantomPlugin>();
}

} // namespace openvr_pair::overlay
