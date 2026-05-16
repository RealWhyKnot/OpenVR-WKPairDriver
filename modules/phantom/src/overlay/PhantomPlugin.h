#pragma once

#include "Config.h"
#include "FeaturePlugin.h"
#include "IPCClient.h"
#include "PhantomStateShmem.h"

#include <chrono>
#include <string>
#include <unordered_map>

class PhantomPlugin final : public openvr_pair::overlay::FeaturePlugin
{
public:
    const char* Name() const override         { return "Phantom Trackers"; }
    const char* FlagFileName() const override { return "enable_phantom.flag"; }
    const char* PipeName() const override     { return OPENVR_PAIRDRIVER_PHANTOM_PIPE_NAME; }

    void OnStart(openvr_pair::overlay::ShellContext& context) override;
    void Tick(openvr_pair::overlay::ShellContext& context) override;
    void DrawTab(openvr_pair::overlay::ShellContext& context) override;

private:
    PhantomConfig    cfg_   = LoadPhantomConfig();
    PhantomIPCClient ipc_;
    std::string      connectError_;

    // Read-side mapping of the per-device state shmem the driver publishes.
    // Opened on first Tick where the driver is up; closed lazily on shutdown.
    phantom::PhantomStateShmem stateShmem_;
    bool stateShmemReady_ = false;

    // Whether we have ever managed to send a config. Drives "reconnect"
    // replay logic to keep the driver consistent after IPC drops.
    bool seededDriver_ = false;

    void DrawDropoutsTab();
    void DrawDiagnosticsTab();
    void DrawAdvancedTab();

    bool ConnectIfNeeded();
    void SendConfig();
    void SendDeviceOptIn(const std::string& serial, bool enabled);
};
