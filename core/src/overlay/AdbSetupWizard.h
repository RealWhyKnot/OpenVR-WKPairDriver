#pragma once

#include "AdbController.h"

#include <array>
#include <string>

namespace wkopenvr::adb {

enum class WizardStep {
    Start        = 0,
    CheckBinary,        // verify shipped adb.exe runnable
    CheckDevAccount,    // adb devices returns anything (account approved)
    CheckDevMode,       // adb devices shows authorized entry
    UsbPair,            // detect USB-paired device
    WifiTcpip,          // adb tcpip 5555
    WifiDiscover,       // resolve quest IP via shell ip route
    WifiPair,           // adb pair <host:pairingPort> with 6-digit code
    WifiVerify,         // user unplugs USB; adb connect + getprop ro.product.model
    Done
};

enum class StepStatus {
    NotStarted,
    InProgress,
    Failed,
    Passed
};

struct StepResult {
    StepStatus  status = StepStatus::NotStarted;
    std::string detail;     // human-readable; for UI display
};

// Result of a one-shot polarity probe written to debug.oculus.guardian_pause.
struct PolarityResult {
    int  writtenValue;      // value we sent to setprop
    int  readBackValue;     // value getprop returned (-1 on failure)
    bool readMatchesWrite;
};

class SetupWizard {
public:
    explicit SetupWizard(AdbController& adb);

    WizardStep currentStep() const;
    StepResult stepResult(WizardStep step) const;

    // Transitions invoked by the UI. Each runs the step and returns its result.
    StepResult RunCheckBinary();
    StepResult RunCheckDevAccount();
    StepResult RunCheckDevMode();
    StepResult RunRetryUsbPrompt(); // restart adb server and poll for USB authorization
    StepResult RunUsbPair();        // verifies adb devices shows an authorized device

    StepResult RunWifiTcpip();
    StepResult RunWifiDiscover();   // populates m_discoveredEndpoint

    // pairingHostPort: "192.168.x.y:<pairingPort>" shown on Quest's pair screen.
    // pairingCode:     6-digit code from that same screen.
    StepResult RunWifiPair(const std::string& pairingHostPort,
                           const std::string& pairingCode);

    StepResult RunWifiVerify(const std::string& endpointOverride = {}); // connect over Wi-Fi, getprop ro.product.model

    // Skip the USB-dependent Wi-Fi setup steps and let the user enter the
    // headset's Wireless ADB pairing host:port, code, and connect endpoint.
    void UseWirelessFallback();

    void Reset();
    bool IsDone() const;

    // Endpoint discovered by RunWifiDiscover(), e.g. "192.168.1.42:5555".
    // The overlay should persist this to CalCtx.adb.savedEndpoint on Done.
    std::string DiscoveredEndpoint() const;

private:
    AdbController& m_adb;
    WizardStep     m_step;

    static constexpr int kStepCount = 10; // one slot per WizardStep enumerator
    std::array<StepResult, kStepCount> m_results;

    std::string m_discoveredEndpoint;

    // Helper: record result for the given step, advance m_step on pass.
    StepResult Commit(WizardStep step, StepResult result);

    // Helper: extract the first non-loopback IP from `ip route` or `ifconfig wlan0` output.
    static std::string ParseIpFromIpRoute(const std::string& output);
    static std::string ParseIpFromIfconfig(const std::string& output);
};

// Stand-alone polarity probe. Writes 1 to debug.oculus.guardian_pause, reads
// it back. The overlay calls this once at wizard completion and stores the
// result so the user can confirm in-headset whether Guardian actually disappeared.
PolarityResult ProbeGuardianPolarity(AdbController& adb);

} // namespace wkopenvr::adb
