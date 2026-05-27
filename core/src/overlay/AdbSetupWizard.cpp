#include "AdbSetupWizard.h"

#include "DiagnosticsLog.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>

namespace wkopenvr::adb {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

enum class UsbDeviceState {
    NoDevice,
    Unauthorized,
    Offline,
    Authorized,
    Other
};

bool Contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::string TrimAsciiForUi(std::string s)
{
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    if (s.size() > 240) {
        s.resize(240);
        s += "...";
    }
    return s;
}

// True if the devices output has at least one non-header, non-empty line.
// Any status (unauthorized, offline, device) counts as "something present."
UsbDeviceState ClassifyDevices(const std::string& output)
{
    std::istringstream ss(output);
    std::string line;
    bool pastHeader = false;
    bool sawUnauthorized = false;
    bool sawOffline = false;
    bool sawOther = false;
    while (std::getline(ss, line)) {
        // Strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "List of devices attached") {
            pastHeader = true;
            continue;
        }
        if (!pastHeader) continue;
        if (line.empty()) continue;

        std::istringstream tok(line);
        std::string serial;
        std::string state;
        tok >> serial >> state;
        if (state == "device") return UsbDeviceState::Authorized;
        if (state == "unauthorized") sawUnauthorized = true;
        else if (state == "offline") sawOffline = true;
        else sawOther = true;
    }
    if (sawUnauthorized) return UsbDeviceState::Unauthorized;
    if (sawOffline) return UsbDeviceState::Offline;
    if (sawOther) return UsbDeviceState::Other;
    return UsbDeviceState::NoDevice;
}

bool DevicesHasAnyEntry(const std::string& output)
{
    return ClassifyDevices(output) != UsbDeviceState::NoDevice;
}

bool DevicesHasAuthorized(const std::string& output)
{
    return ClassifyDevices(output) == UsbDeviceState::Authorized;
}

const char* UsbStateLabel(UsbDeviceState state)
{
    switch (state) {
    case UsbDeviceState::NoDevice:      return "no device";
    case UsbDeviceState::Unauthorized:  return "unauthorized";
    case UsbDeviceState::Offline:       return "offline";
    case UsbDeviceState::Authorized:    return "authorized";
    case UsbDeviceState::Other:         return "unknown";
    }
    return "unknown";
}

const char* StepStatusLabel(StepStatus status)
{
    switch (status) {
    case StepStatus::NotStarted: return "not_started";
    case StepStatus::InProgress: return "in_progress";
    case StepStatus::Passed:     return "passed";
    case StepStatus::Failed:     return "failed";
    }
    return "unknown";
}

std::string UsbStateHelp(UsbDeviceState state)
{
    switch (state) {
    case UsbDeviceState::NoDevice:
        return "No Quest detected. Use a USB-C data cable, unlock the headset, and keep Developer Mode enabled in the Meta Horizon app.";
    case UsbDeviceState::Unauthorized:
        return "Quest is visible but unauthorized. Put on the headset, unlock it, and accept 'Allow USB debugging?'. MTP Notification is only for file-transfer prompts; it does not authorize ADB. If the prompt still does not appear, leave USB plugged in and toggle Developer Mode off/on in the Meta Horizon app, then retry.";
    case UsbDeviceState::Offline:
        return "Quest is visible but offline. Replug USB, unlock the headset, and retry. If it stays offline, reboot the headset or try a different USB port/cable.";
    case UsbDeviceState::Authorized:
        return "USB debugging authorized.";
    case UsbDeviceState::Other:
        return "ADB sees a device, but not in an authorized Quest state. Replug USB, unlock the headset, and retry.";
    }
    return "Unknown USB state.";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// SetupWizard
// ---------------------------------------------------------------------------

SetupWizard::SetupWizard(AdbController& adb)
    : m_adb(adb)
    , m_step(WizardStep::Start)
{
    m_results.fill(StepResult{StepStatus::NotStarted, {}});
}

WizardStep SetupWizard::currentStep() const
{
    return m_step;
}

StepResult SetupWizard::stepResult(WizardStep step) const
{
    const int idx = static_cast<int>(step);
    if (idx < 0 || idx >= kStepCount) return {};
    return m_results[idx];
}

std::string SetupWizard::DiscoveredEndpoint() const
{
    return m_discoveredEndpoint;
}

bool SetupWizard::IsDone() const
{
    return m_step == WizardStep::Done;
}

void SetupWizard::Reset()
{
    m_step = WizardStep::Start;
    m_results.fill(StepResult{StepStatus::NotStarted, {}});
    m_discoveredEndpoint.clear();
    m_manualWirelessPairing = false;
    fprintf(stderr, "[adb-wizard] reset to Start\n");
    openvr_pair::common::DiagnosticLog("adb-wizard", "reset step=start");
}

StepResult SetupWizard::Commit(WizardStep step, StepResult result)
{
    const int idx = static_cast<int>(step);
    assert(idx >= 0 && idx < kStepCount);
    m_results[idx] = result;

    if (result.status == StepStatus::Passed) {
        // Advance to the next step.
        m_step = static_cast<WizardStep>(static_cast<int>(step) + 1);
        fprintf(stderr, "[adb-wizard] step %d passed -> now at step %d\n",
                idx, static_cast<int>(m_step));
    } else {
        // Stay on the current step so the UI can retry.
        fprintf(stderr, "[adb-wizard] step %d failed: %s\n",
                idx, result.detail.c_str());
    }
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "step_result step=%d status=%s next_step=%d detail='%s'",
        idx,
        StepStatusLabel(result.status),
        static_cast<int>(m_step),
        result.detail.c_str());
    return result;
}

void SetupWizard::UseWirelessFallback()
{
    m_discoveredEndpoint.clear();
    m_manualWirelessPairing = true;
    m_step = WizardStep::WifiPair;
    fprintf(stderr, "[adb-wizard] USB authorization bypassed -> manual wireless pairing fallback\n");
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "manual_wireless_pairing selected step=%d",
        static_cast<int>(m_step));
}

void SetupWizard::UseCurrentAdbConnection()
{
    m_manualWirelessPairing = false;
    m_results[static_cast<int>(WizardStep::CheckBinary)] =
        StepResult{StepStatus::Passed, "Skipped because ADB is already connected."};
    m_results[static_cast<int>(WizardStep::CheckDevAccount)] =
        StepResult{StepStatus::Passed, "Skipped because ADB already sees the Quest."};
    m_results[static_cast<int>(WizardStep::CheckDevMode)] =
        StepResult{StepStatus::Passed, "Skipped because USB debugging is already authorized."};
    m_results[static_cast<int>(WizardStep::UsbPair)] =
        StepResult{StepStatus::Passed, "Skipped because ADB is already connected."};
    m_step = WizardStep::WifiTcpip;
    fprintf(stderr, "[adb-wizard] current ADB connection accepted -> step %d\n",
            static_cast<int>(m_step));
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "current_adb_connection accepted step=%d",
        static_cast<int>(m_step));
}

// ---------------------------------------------------------------------------
// CheckBinary
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunCheckBinary()
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_check_binary");
    if (!m_adb.BinaryAvailable()) {
        r.status = StepStatus::Failed;
        r.detail = "adb.exe not found -- reinstall WKOpenVR.";
        return Commit(WizardStep::CheckBinary, r);
    }

    const auto result = m_adb.Run({"version"}, std::chrono::seconds(5));
    if (result.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb version timed out.";
        return Commit(WizardStep::CheckBinary, r);
    }
    if (result.exitCode != 0 || !Contains(result.out, "Android Debug Bridge")) {
        r.status = StepStatus::Failed;
        r.detail = "adb.exe did not return expected version output.";
        return Commit(WizardStep::CheckBinary, r);
    }

    r.status = StepStatus::Passed;
    r.detail = "adb binary OK.";
    return Commit(WizardStep::CheckBinary, r);
}

// ---------------------------------------------------------------------------
// CheckDevAccount
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunCheckDevAccount()
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_check_dev_account");
    const auto result = m_adb.Run({"devices"}, std::chrono::seconds(5));
    if (result.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb devices timed out.";
        return Commit(WizardStep::CheckDevAccount, r);
    }
    if (result.exitCode != 0) {
        r.status = StepStatus::Failed;
        r.detail = "adb devices failed (exit " + std::to_string(result.exitCode) + ").";
        return Commit(WizardStep::CheckDevAccount, r);
    }

    if (!DevicesHasAnyEntry(result.out)) {
        r.status = StepStatus::Failed;
        r.detail = "No Quest detected -- turn Developer Mode on in the Meta Horizon app, then reconnect USB.";
        return Commit(WizardStep::CheckDevAccount, r);
    }

    r.status = StepStatus::Passed;
    r.detail = std::string("Device visible to adb (state: ") +
               UsbStateLabel(ClassifyDevices(result.out)) + ").";
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "dev_account_device_state state=%s",
        UsbStateLabel(ClassifyDevices(result.out)));
    return Commit(WizardStep::CheckDevAccount, r);
}

// ---------------------------------------------------------------------------
// CheckDevMode
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunCheckDevMode()
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_check_dev_mode");
    const auto result = m_adb.Run({"devices"}, std::chrono::seconds(5));
    if (result.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb devices timed out.";
        return Commit(WizardStep::CheckDevMode, r);
    }
    if (result.exitCode != 0) {
        r.status = StepStatus::Failed;
        r.detail = "adb devices failed (exit " + std::to_string(result.exitCode) + ").";
        return Commit(WizardStep::CheckDevMode, r);
    }

    const UsbDeviceState state = ClassifyDevices(result.out);
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "dev_mode_device_state state=%s",
        UsbStateLabel(state));
    if (state == UsbDeviceState::Unauthorized) {
        r.status = StepStatus::Failed;
        r.detail = UsbStateHelp(state);
        return Commit(WizardStep::CheckDevMode, r);
    }

    if (state != UsbDeviceState::Authorized) {
        r.status = StepStatus::Failed;
        r.detail = UsbStateHelp(state);
        return Commit(WizardStep::CheckDevMode, r);
    }

    r.status = StepStatus::Passed;
    r.detail = "Developer mode confirmed.";
    return Commit(WizardStep::CheckDevMode, r);
}

StepResult SetupWizard::RunRetryUsbPrompt()
{
    StepResult r;
    r.status = StepStatus::InProgress;
    const WizardStep origin = m_step;
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "retry_usb_prompt origin_step=%d",
        static_cast<int>(origin));
    const auto commitFailure = [&](StepResult result) {
        StepResult committed = Commit(WizardStep::CheckDevMode, result);
        if (origin == WizardStep::UsbPair) {
            m_step = WizardStep::CheckDevMode;
        }
        return committed;
    };

    const auto kill = m_adb.Run({"kill-server"}, std::chrono::seconds(3));
    if (kill.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb kill-server timed out. Unplug USB, reconnect it, then retry.";
        return commitFailure(r);
    }

    const auto start = m_adb.Run({"start-server"}, std::chrono::seconds(5));
    if (start.timedOut || start.exitCode != 0) {
        r.status = StepStatus::Failed;
        r.detail = "adb start-server failed. Replug USB or restart WKOpenVR, then retry.";
        return commitFailure(r);
    }

    UsbDeviceState lastState = UsbDeviceState::NoDevice;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto devices = m_adb.Run({"devices", "-l"}, std::chrono::seconds(3));
        if (!devices.timedOut && devices.exitCode == 0) {
            lastState = ClassifyDevices(devices.out);
            if (lastState == UsbDeviceState::Authorized) {
                r.status = StepStatus::Passed;
                r.detail = "USB debugging authorized. Continuing setup.";
                openvr_pair::common::DiagnosticLog("adb-wizard",
                    "retry_usb_prompt authorized attempt=%d",
                    attempt + 1);
                return Commit(WizardStep::CheckDevMode, r);
            }
        }

        if (attempt < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
        }
    }

    r.status = StepStatus::Failed;
    r.detail = std::string("USB prompt retry finished; state is ") +
               UsbStateLabel(lastState) + ". " + UsbStateHelp(lastState);
    return commitFailure(r);
}

// ---------------------------------------------------------------------------
// UsbPair
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunUsbPair()
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_usb_pair");
    const auto result = m_adb.Run({"devices"}, std::chrono::seconds(5));
    if (result.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb devices timed out.";
        return Commit(WizardStep::UsbPair, r);
    }

    const UsbDeviceState state = ClassifyDevices(result.out);
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "usb_pair_device_state state=%s",
        UsbStateLabel(state));
    if (state != UsbDeviceState::Authorized) {
        r.status = StepStatus::Failed;
        r.detail = std::string("Quest not paired over USB (state: ") +
                   UsbStateLabel(state) + "). " + UsbStateHelp(state);
        return Commit(WizardStep::UsbPair, r);
    }

    r.status = StepStatus::Passed;
    r.detail = "USB pairing confirmed.";
    return Commit(WizardStep::UsbPair, r);
}

// ---------------------------------------------------------------------------
// WifiTcpip
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunWifiTcpip()
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_wifi_tcpip");
    const auto result = m_adb.Run({"tcpip", "5555"}, std::chrono::seconds(5));
    if (result.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb tcpip timed out.";
        return Commit(WizardStep::WifiTcpip, r);
    }
    if (result.exitCode != 0) {
        r.status = StepStatus::Failed;
        r.detail = "adb tcpip 5555 failed (exit " + std::to_string(result.exitCode) + ").";
        return Commit(WizardStep::WifiTcpip, r);
    }

    r.status = StepStatus::Passed;
    r.detail = "Wi-Fi ADB port set to 5555. This USB-authorized path does not need a pairing code.";
    return Commit(WizardStep::WifiTcpip, r);
}

// ---------------------------------------------------------------------------
// WifiDiscover
// ---------------------------------------------------------------------------

// static
std::string SetupWizard::ParseIpFromIpRoute(const std::string& output)
{
    // Parse `ip route` output.
    // Look for a line containing "wlan" (preferred) or any non-loopback route.
    // The gateway/source IP we want is on the "src" field, or we can extract
    // the device's IP from the "dev wlan0 src <ip>" pattern.
    // Fallback: look for "default via <gw> dev wlan0 src <ip>" or
    //           "src <ip>" anywhere on a wlan line.
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!Contains(line, "wlan")) continue;

        // Try to find "src <ip>" on the line.
        const std::string srcToken = " src ";
        const size_t srcPos = line.find(srcToken);
        if (srcPos != std::string::npos) {
            std::string rest = line.substr(srcPos + srcToken.size());
            // The IP is the first whitespace-delimited token.
            std::istringstream tok(rest);
            std::string ip;
            tok >> ip;
            if (!ip.empty() && ip.find('.') != std::string::npos) {
                return ip;
            }
        }

        // Fallback: the route itself may be "192.168.x.0/24 dev wlan0 ..."
        // Extract the network address and strip the mask.
        std::istringstream tok(line);
        std::string first;
        tok >> first;
        const size_t slash = first.find('/');
        if (slash != std::string::npos) {
            first = first.substr(0, slash);
        }
        if (!first.empty() && first.find('.') != std::string::npos
            && first != "0.0.0.0") {
            // This is a network prefix, not the device IP -- skip.
            // We'll only fall back to this if there's nothing better.
        }
    }
    return {};
}

// static
std::string SetupWizard::ParseIpFromIfconfig(const std::string& output)
{
    // Parse `ifconfig wlan0` output.
    // Look for "inet addr:<ip>" (older busybox) or "inet <ip>" (newer iproute2-style).
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // "inet addr:192.168.x.y" (busybox style)
        {
            const std::string token = "inet addr:";
            const size_t pos = line.find(token);
            if (pos != std::string::npos) {
                std::string rest = line.substr(pos + token.size());
                std::istringstream tok(rest);
                std::string ip;
                tok >> ip;
                if (!ip.empty() && ip.find('.') != std::string::npos
                    && ip != "127.0.0.1") {
                    return ip;
                }
            }
        }

        // "inet 192.168.x.y" (newer style, also appears in some iproute output)
        {
            const std::string token = "inet ";
            const size_t pos = line.find(token);
            if (pos != std::string::npos) {
                std::string rest = line.substr(pos + token.size());
                std::istringstream tok(rest);
                std::string ip;
                tok >> ip;
                // Strip mask suffix if present ("192.168.x.y/24")
                const size_t slash = ip.find('/');
                if (slash != std::string::npos) ip = ip.substr(0, slash);
                if (!ip.empty() && ip.find('.') != std::string::npos
                    && ip != "127.0.0.1") {
                    return ip;
                }
            }
        }
    }
    return {};
}

StepResult SetupWizard::RunWifiDiscover()
{
    StepResult r;
    r.status = StepStatus::InProgress;
    m_discoveredEndpoint.clear();
    openvr_pair::common::DiagnosticLog("adb-wizard", "run_wifi_discover");

    // Try `ip route` first.
    auto result = m_adb.Shell("ip route", std::chrono::seconds(5));
    std::string ip;

    if (!result.timedOut && result.exitCode == 0) {
        ip = ParseIpFromIpRoute(result.out);
    }

    // Fall back to `ifconfig wlan0`.
    if (ip.empty()) {
        result = m_adb.Shell("ifconfig wlan0", std::chrono::seconds(5));
        if (!result.timedOut && result.exitCode == 0) {
            ip = ParseIpFromIfconfig(result.out);
        }
    }

    if (ip.empty()) {
        r.status = StepStatus::Failed;
        r.detail = "Could not determine Quest Wi-Fi IP. Make sure the Quest is on Wi-Fi.";
        return Commit(WizardStep::WifiDiscover, r);
    }

    m_discoveredEndpoint = ip + ":5555";
    r.status = StepStatus::Passed;
    r.detail = "Quest IP: " + ip + " (endpoint: " + m_discoveredEndpoint + "). Pairing code not required.";
    StepResult committed = Commit(WizardStep::WifiDiscover, r);
    if (committed.status == StepStatus::Passed && !m_manualWirelessPairing) {
        m_results[static_cast<int>(WizardStep::WifiPair)] =
            StepResult{StepStatus::Passed, "Skipped for USB-authorized tcpip wireless ADB."};
        m_step = WizardStep::WifiVerify;
        fprintf(stderr, "[adb-wizard] skipping manual wireless pair -> now at step %d\n",
                static_cast<int>(m_step));
        openvr_pair::common::DiagnosticLog("adb-wizard",
            "manual_wireless_pair skipped reason=usb_authorized_tcpip step=%d",
            static_cast<int>(m_step));
    }
    return committed;
}

// ---------------------------------------------------------------------------
// WifiPair
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunWifiPair(const std::string& pairingHostPort,
                                    const std::string& pairingCode)
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_wifi_pair");
    if (pairingHostPort.empty() || pairingCode.empty()) {
        r.status = StepStatus::Failed;
        r.detail = "Pairing host:port and 6-digit code are required.";
        return Commit(WizardStep::WifiPair, r);
    }

    // `adb pair <host:pairingPort>` reads the code from stdin.
    // We pass the code via a shell pipe because there is no stdin in our subprocess model:
    // `echo <code> | adb pair <host:port>` -- but adb pair reads from stdin.
    // The cleanest approach with our CreateProcessW model is to pass pairingCode via
    // stdin of the child. AdbController does not currently wire stdin; adb pair on
    // newer versions also accepts the code as a second argument on some builds.
    // Use: `adb pair <host:port> <code>` (supported in platform-tools >= 31).
    const auto result = m_adb.Run({"pair", pairingHostPort, pairingCode},
                                  std::chrono::seconds(10));

    if (result.timedOut) {
        r.status = StepStatus::Failed;
        r.detail = "adb pair timed out. Confirm the pairing code is still shown on the Quest.";
        return Commit(WizardStep::WifiPair, r);
    }
    if (result.exitCode != 0 || Contains(result.out, "Failed")) {
        r.status = StepStatus::Failed;
        r.detail = "Pairing failed. Verify the code and that the Quest pairing screen is open. "
                   "adb output: " + result.out;
        return Commit(WizardStep::WifiPair, r);
    }
    if (!Contains(result.out, "Successfully paired") &&
        !Contains(result.out, "paired") &&
        !Contains(result.out, "success")) {
        r.status = StepStatus::Failed;
        r.detail = "Unexpected adb pair output: " + result.out;
        return Commit(WizardStep::WifiPair, r);
    }

    r.status = StepStatus::Passed;
    r.detail = "Wi-Fi paired with " + pairingHostPort + ".";
    return Commit(WizardStep::WifiPair, r);
}

// ---------------------------------------------------------------------------
// WifiVerify
// ---------------------------------------------------------------------------
StepResult SetupWizard::RunWifiVerify(const std::string& endpointOverride)
{
    StepResult r;
    r.status = StepStatus::InProgress;

    openvr_pair::common::DiagnosticLog("adb-wizard", "run_wifi_verify");
    std::string endpoint = endpointOverride.empty() ? m_discoveredEndpoint : endpointOverride;
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "wifi_verify_endpoint endpoint='%s' override=%d discovered_empty=%d",
        endpoint.c_str(),
        endpointOverride.empty() ? 0 : 1,
        m_discoveredEndpoint.empty() ? 1 : 0);
    if (endpoint.empty()) {
        r.status = StepStatus::Failed;
        r.detail = "No endpoint discovered. Re-run Discover while USB ADB is authorized, or use the manual pairing-code fallback only if the Quest shows an Android Wireless debugging screen.";
        return Commit(WizardStep::WifiVerify, r);
    }

    if (!endpointOverride.empty()) {
        m_discoveredEndpoint = endpoint;
    }

    if (!m_adb.Connect(endpoint)) {
        openvr_pair::common::DiagnosticLog("adb-wizard",
            "wifi_verify_connect_failed endpoint='%s'", endpoint.c_str());
        r.status = StepStatus::Failed;
        r.detail = "Failed to connect to " + endpoint +
                   ". Ensure the Quest is awake and on the same Wi-Fi network.";
        return Commit(WizardStep::WifiVerify, r);
    }

    const auto prop = m_adb.Shell("getprop ro.product.model", std::chrono::seconds(5));
    if (prop.timedOut || prop.exitCode != 0) {
        openvr_pair::common::DiagnosticLog("adb-wizard",
            "wifi_verify_getprop_failed timed_out=%d exit=%d stdout='%s' stderr='%s'",
            prop.timedOut ? 1 : 0,
            prop.exitCode,
            TrimAsciiForUi(prop.out).c_str(),
            TrimAsciiForUi(prop.err).c_str());
        r.status = StepStatus::Failed;
        r.detail = "Connected but getprop timed out or failed.";
        return Commit(WizardStep::WifiVerify, r);
    }

    const std::string model = prop.out;
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "wifi_verify_model raw='%s'", TrimAsciiForUi(model).c_str());
    if (!Contains(model, "Quest")) {
        r.status = StepStatus::Failed;
        r.detail = "Connected but device does not look like a Quest (model: " + model + ").";
        return Commit(WizardStep::WifiVerify, r);
    }

    r.status = StepStatus::Passed;
    r.detail = "Wi-Fi connection verified -- Quest model: " + [&]{
        std::string m = model;
        while (!m.empty() && (m.back() == '\r' || m.back() == '\n' || m.back() == ' '))
            m.pop_back();
        return m;
    }() + ".";
    return Commit(WizardStep::WifiVerify, r);
}

// ---------------------------------------------------------------------------
// ProbeGuardianPolarity
// ---------------------------------------------------------------------------
PolarityResult ProbeGuardianPolarity(AdbController& adb)
{
    PolarityResult out{};
    out.writtenValue    = 1;
    out.readBackValue   = -1;
    out.readMatchesWrite = false;

    // Write 1; read back.
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "guardian_probe_start write_value=%d", out.writtenValue);
    if (!adb.SetGuardianPaused(true, out.writtenValue)) {
        fprintf(stderr, "[adb-wizard] ProbeGuardianPolarity: SetGuardianPaused failed\n");
        openvr_pair::common::DiagnosticLog("adb-wizard",
            "guardian_probe_set_failed write_value=%d", out.writtenValue);
        return out;
    }

    out.readBackValue    = adb.GetGuardianPaused();
    out.readMatchesWrite = (out.readBackValue == out.writtenValue);

    fprintf(stderr, "[adb-wizard] ProbeGuardianPolarity: wrote=%d read=%d match=%d\n",
            out.writtenValue, out.readBackValue, out.readMatchesWrite ? 1 : 0);
    openvr_pair::common::DiagnosticLog("adb-wizard",
        "guardian_probe_result wrote=%d read=%d match=%d",
        out.writtenValue,
        out.readBackValue,
        out.readMatchesWrite ? 1 : 0);
    return out;
}

} // namespace wkopenvr::adb
