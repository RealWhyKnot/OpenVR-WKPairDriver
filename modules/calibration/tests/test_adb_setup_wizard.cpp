// Tests for AdbSetupWizard state machine.
//
// A StubAdbController overrides Run to inject canned outputs. No real adb
// binary is needed; no subprocess is spawned.

#include "AdbController.h"
#include "AdbSetupWizard.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

// Stub: records the last call's args; returns configurable canned output.
class StubAdb : public AdbController {
public:
    std::string  stubOut;
    std::string  stubErr;
    int          stubExit    = 0;
    bool         stubTimeout = false;
    bool         binaryExists = true;

    // Track calls for assertion convenience.
    mutable std::vector<std::vector<std::string>> calls;

    bool BinaryAvailable() const override { return binaryExists; }

    AdbController::Result Run(const std::vector<std::string>& args,
                              std::chrono::milliseconds /*timeout*/) override
    {
        calls.push_back(args);
        AdbController::Result r;
        r.out      = stubOut;
        r.err      = stubErr;
        r.exitCode = stubExit;
        r.timedOut = stubTimeout;
        return r;
    }
};

class SequenceAdb : public AdbController {
public:
    std::vector<AdbController::Result> results;
    mutable std::vector<std::vector<std::string>> calls;

    bool BinaryAvailable() const override { return true; }

    AdbController::Result Run(const std::vector<std::string>& args,
                              std::chrono::milliseconds /*timeout*/) override
    {
        calls.push_back(args);
        if (calls.size() <= results.size()) {
            return results[calls.size() - 1];
        }
        AdbController::Result r;
        r.exitCode = 0;
        return r;
    }
};

AdbController::Result AdbResult(std::string out, int exitCode = 0, bool timedOut = false)
{
    AdbController::Result r;
    r.out = std::move(out);
    r.exitCode = exitCode;
    r.timedOut = timedOut;
    return r;
}

} // namespace

using namespace wkopenvr::adb;

// ---------------------------------------------------------------------------
// Reset_returns_to_start
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, Reset_returns_to_start)
{
    StubAdb adb;
    adb.stubOut  = "Android Debug Bridge version 1.0.41\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    wiz.RunCheckBinary();
    EXPECT_EQ(wiz.currentStep(), WizardStep::CheckDevAccount);

    wiz.Reset();
    EXPECT_EQ(wiz.currentStep(), WizardStep::Start);
    EXPECT_EQ(wiz.stepResult(WizardStep::CheckBinary).status, StepStatus::NotStarted);
    EXPECT_TRUE(wiz.DiscoveredEndpoint().empty());
}

// ---------------------------------------------------------------------------
// CheckBinary_passes_on_real_version_output
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckBinary_passes_on_real_version_output)
{
    StubAdb adb;
    adb.stubOut  = "Android Debug Bridge version 1.0.41\nRevision 33.0.3-android-tools\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckBinary();

    EXPECT_EQ(r.status, StepStatus::Passed);
    EXPECT_EQ(wiz.currentStep(), WizardStep::CheckDevAccount);
}

// ---------------------------------------------------------------------------
// CheckBinary_fails_on_garbage
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckBinary_fails_on_garbage)
{
    StubAdb adb;
    adb.stubOut  = "not adb\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckBinary();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_EQ(wiz.currentStep(), WizardStep::Start)
        << "Wizard should stay at Start after a failed CheckBinary";
}

// ---------------------------------------------------------------------------
// CheckBinary_fails_when_binary_not_available
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckBinary_fails_when_binary_not_available)
{
    StubAdb adb;
    adb.binaryExists = false;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckBinary();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("not found"), std::string::npos);
}

// ---------------------------------------------------------------------------
// CheckDevAccount_fails_on_empty_devices
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckDevAccount_fails_on_empty_devices)
{
    StubAdb adb;
    adb.stubOut  = "List of devices attached\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckDevAccount();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("No Quest detected"), std::string::npos);
}

// ---------------------------------------------------------------------------
// CheckDevAccount_passes_on_unauthorized
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckDevAccount_passes_on_unauthorized)
{
    StubAdb adb;
    adb.stubOut  = "List of devices attached\nABCD1234\tunauthorized\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckDevAccount();

    EXPECT_EQ(r.status, StepStatus::Passed)
        << "An unauthorized entry still means the dev account is approved";
}

// ---------------------------------------------------------------------------
// CheckDevMode_fails_on_unauthorized
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckDevMode_fails_on_unauthorized)
{
    StubAdb adb;
    adb.stubOut  = "List of devices attached\nABCD1234\tunauthorized\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckDevMode();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("unauthorized"), std::string::npos);
    EXPECT_NE(r.detail.find("Allow USB debugging"), std::string::npos);
    EXPECT_NE(r.detail.find("MTP Notification"), std::string::npos);
}

TEST(AdbSetupWizardTest, CheckDevMode_fails_on_offline)
{
    StubAdb adb;
    adb.stubOut  = "List of devices attached\nABCD1234\toffline\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckDevMode();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("offline"), std::string::npos);
}

// ---------------------------------------------------------------------------
// CheckDevMode_passes_on_authorized_device
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, CheckDevMode_passes_on_authorized_device)
{
    StubAdb adb;
    adb.stubOut  = "List of devices attached\nABCD1234\tdevice\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunCheckDevMode();

    EXPECT_EQ(r.status, StepStatus::Passed);
}

TEST(AdbSetupWizardTest, RetryUsbPrompt_restarts_server_and_passes_when_authorized)
{
    SequenceAdb adb;
    adb.results = {
        AdbResult(""), // kill-server
        AdbResult("* daemon started successfully *\n"), // start-server
        AdbResult("List of devices attached\nABCD1234\tunauthorized usb:1-1\n"),
        AdbResult("List of devices attached\nABCD1234\tdevice product:hollywood model:Quest_3\n")
    };

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunRetryUsbPrompt();

    ASSERT_EQ(r.status, StepStatus::Passed) << r.detail;
    EXPECT_EQ(wiz.currentStep(), WizardStep::UsbPair);
    ASSERT_GE(adb.calls.size(), 4u);
    EXPECT_EQ(adb.calls[0], (std::vector<std::string>{"kill-server"}));
    EXPECT_EQ(adb.calls[1], (std::vector<std::string>{"start-server"}));
    EXPECT_EQ(adb.calls[2], (std::vector<std::string>{"devices", "-l"}));
}

TEST(AdbSetupWizardTest, RetryUsbPrompt_reports_final_state_when_still_unauthorized)
{
    SequenceAdb adb;
    adb.results = {
        AdbResult(""),
        AdbResult("* daemon started successfully *\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n")
    };

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunRetryUsbPrompt();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("unauthorized"), std::string::npos);
    EXPECT_EQ(wiz.currentStep(), WizardStep::Start);
}

TEST(AdbSetupWizardTest, RetryUsbPrompt_from_usb_pair_returns_to_authorization_on_failure)
{
    SequenceAdb adb;
    adb.results = {
        AdbResult("List of devices attached\nABCD1234\tdevice product:hollywood model:Quest_3\n"),
        AdbResult(""),
        AdbResult("* daemon started successfully *\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n"),
        AdbResult("List of devices attached\nABCD1234\tunauthorized\n")
    };

    SetupWizard wiz(adb);
    ASSERT_EQ(wiz.RunCheckDevMode().status, StepStatus::Passed);
    ASSERT_EQ(wiz.currentStep(), WizardStep::UsbPair);

    const StepResult r = wiz.RunRetryUsbPrompt();

    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("unauthorized"), std::string::npos);
    EXPECT_EQ(wiz.currentStep(), WizardStep::CheckDevMode);
    EXPECT_NE(wiz.stepResult(WizardStep::CheckDevMode).detail.find("unauthorized"),
              std::string::npos);
}

TEST(AdbSetupWizardTest, UseWirelessFallback_jumps_to_wifi_pair)
{
    StubAdb adb;
    SetupWizard wiz(adb);

    wiz.UseWirelessFallback();

    EXPECT_EQ(wiz.currentStep(), WizardStep::WifiPair);
    EXPECT_TRUE(wiz.DiscoveredEndpoint().empty());
}

TEST(AdbSetupWizardTest, UseCurrentAdbConnection_jumps_to_wifi_tcpip)
{
    StubAdb adb;
    SetupWizard wiz(adb);

    wiz.UseCurrentAdbConnection();

    EXPECT_EQ(wiz.currentStep(), WizardStep::WifiTcpip);
    EXPECT_EQ(wiz.stepResult(WizardStep::CheckBinary).status, StepStatus::Passed);
    EXPECT_EQ(wiz.stepResult(WizardStep::CheckDevAccount).status, StepStatus::Passed);
    EXPECT_EQ(wiz.stepResult(WizardStep::CheckDevMode).status, StepStatus::Passed);
    EXPECT_EQ(wiz.stepResult(WizardStep::UsbPair).status, StepStatus::Passed);
}

// ---------------------------------------------------------------------------
// WifiDiscover_parses_ip_route
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, WifiDiscover_parses_ip_route)
{
    StubAdb adb;
    // Realistic `ip route` output from a Quest 3.
    adb.stubOut =
        "default via 192.168.1.1 dev wlan0 table 1003\n"
        "192.168.1.0/24 dev wlan0 proto kernel scope link src 192.168.1.42\n";
    adb.stubExit = 0;

    SetupWizard wiz(adb);
    const StepResult r = wiz.RunWifiDiscover();

    EXPECT_EQ(r.status, StepStatus::Passed) << "detail: " << r.detail;
    EXPECT_EQ(wiz.DiscoveredEndpoint(), "192.168.1.42:5555");
    EXPECT_EQ(wiz.currentStep(), WizardStep::WifiVerify);
    EXPECT_EQ(wiz.stepResult(WizardStep::WifiPair).status, StepStatus::Passed);
}

// ---------------------------------------------------------------------------
// WifiDiscover_falls_back_to_ifconfig
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, WifiDiscover_falls_back_to_ifconfig)
{
    // First call (ip route) returns empty; second call (ifconfig) returns IP.
    class TwoCallStub : public AdbController {
    public:
        int callIndex = 0;

        AdbController::Result Run(const std::vector<std::string>& args,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            if (callIndex == 0) {
                // First: `shell ip route` -- no wlan line
                r.out = "default via 10.0.0.1 dev eth0\n";
            } else {
                // Second: `shell ifconfig wlan0`
                r.out = "wlan0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500\n"
                        "        inet 192.168.2.99  netmask 255.255.255.0  broadcast 192.168.2.255\n";
            }
            ++callIndex;
            return r;
        }
    };

    TwoCallStub adb;
    SetupWizard wiz(adb);
    const StepResult r = wiz.RunWifiDiscover();

    EXPECT_EQ(r.status, StepStatus::Passed) << "detail: " << r.detail;
    EXPECT_EQ(wiz.DiscoveredEndpoint(), "192.168.2.99:5555");
    EXPECT_EQ(wiz.currentStep(), WizardStep::WifiVerify);
}

// ---------------------------------------------------------------------------
// WifiVerify_passes_on_quest_model
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, WifiVerify_passes_on_quest_model)
{
    // Wizard must have an endpoint from a prior Discover step.
    // Use a variant stub that returns different outputs per command.
    class VerifyStub : public AdbController {
    public:
        int callIndex = 0;

        // Seed the endpoint into the wizard by rigging WifiDiscover first.
        // For the verify step:
        //   call 0 -> `connect` returns "connected to ..."
        //   call 1 -> `shell getprop ...` returns "Quest 3"
        AdbController::Result Run(const std::vector<std::string>& args,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            if (callIndex == 0) {
                r.out = "connected to 192.168.1.42:5555\n";
            } else {
                r.out = "Quest 3\n";
            }
            ++callIndex;
            return r;
        }
    };

    VerifyStub adb;
    SetupWizard wiz(adb);
    // Manually inject the discovered endpoint (simulates WifiDiscover having run).
    // The only way to set it is RunWifiDiscover, so we use a helper that injects
    // ip route output pointing to the right IP.
    {
        // A quick sub-stub just for WifiDiscover.
        class IpStub : public AdbController {
        public:
            AdbController::Result Run(const std::vector<std::string>&,
                                      std::chrono::milliseconds) override
            {
                AdbController::Result r;
                r.exitCode = 0;
                r.out = "192.168.1.0/24 dev wlan0 proto kernel scope link src 192.168.1.42\n";
                return r;
            }
        };
        IpStub ipAdb;
        SetupWizard tmp(ipAdb);
        tmp.RunWifiDiscover();
        // Now promote the result into our target wizard via the VerifyStub.
        // We cannot directly set m_discoveredEndpoint (it's private), so we
        // just run a second wizard with VerifyStub and pre-inject the endpoint.
        // Instead: inherit and expose a setter just for tests.
    }

    // Alternate approach: subclass SetupWizard to expose SetDiscoveredEndpoint.
    class TestableWizard : public SetupWizard {
    public:
        explicit TestableWizard(AdbController& a) : SetupWizard(a) {}
        void SetEndpoint(const std::string& ep) { m_discoveredEndpoint = ep; }
    private:
        // Expose private member via friendship.
        friend class WifiVerifyTest_Passes;
        std::string m_discoveredEndpoint; // shadows parent -- won't work
    };
    // The cleanest approach: just run WifiDiscover on the same stub, then WifiVerify.
    // But VerifyStub only handles 2 calls.  Use a full-sequence stub instead.

    // --- restart with a clean sequence stub ---
    class FullSeqStub : public AdbController {
    public:
        int callIdx = 0;
        // Calls in order:
        //   0: shell ip route  (WifiDiscover)
        //   1: connect         (WifiVerify)
        //   2: shell getprop   (WifiVerify)
        AdbController::Result Run(const std::vector<std::string>&,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            switch (callIdx++) {
            case 0: r.out = "192.168.1.0/24 dev wlan0 src 192.168.1.42\n"; break;
            case 1: r.out = "connected to 192.168.1.42:5555\n"; break;
            case 2: r.out = "Quest 3\n"; break;
            default: r.out = ""; break;
            }
            return r;
        }
    };

    FullSeqStub seq;
    SetupWizard wiz2(seq);
    wiz2.RunWifiDiscover();
    ASSERT_EQ(wiz2.DiscoveredEndpoint(), "192.168.1.42:5555");

    const StepResult r = wiz2.RunWifiVerify();
    EXPECT_EQ(r.status, StepStatus::Passed) << "detail: " << r.detail;
}

TEST(AdbSetupWizardTest, WifiVerify_uses_manual_endpoint_after_wireless_fallback)
{
    SequenceAdb adb;
    adb.results = {
        AdbResult("Successfully paired to 192.168.1.10:37123\n"),
        AdbResult("connected to 192.168.1.10:44999\n"),
        AdbResult("Quest 3\n")
    };

    SetupWizard wiz(adb);
    wiz.UseWirelessFallback();
    ASSERT_EQ(wiz.RunWifiPair("192.168.1.10:37123", "123456").status, StepStatus::Passed);

    const StepResult r = wiz.RunWifiVerify("192.168.1.10:44999");

    EXPECT_EQ(r.status, StepStatus::Passed) << r.detail;
    EXPECT_EQ(wiz.DiscoveredEndpoint(), "192.168.1.10:44999");
    ASSERT_GE(adb.calls.size(), 3u);
    EXPECT_EQ(adb.calls[1], (std::vector<std::string>{"connect", "192.168.1.10:44999"}));
    EXPECT_EQ(adb.calls[2],
              (std::vector<std::string>{
                  "-s", "192.168.1.10:44999", "shell", "getprop ro.product.model"}));
}

// ---------------------------------------------------------------------------
// WifiVerify_fails_on_non_quest_model
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, WifiVerify_fails_on_non_quest_model)
{
    class FullSeqStub : public AdbController {
    public:
        int callIdx = 0;
        AdbController::Result Run(const std::vector<std::string>&,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            switch (callIdx++) {
            case 0: r.out = "192.168.1.0/24 dev wlan0 src 10.0.0.5\n"; break;
            case 1: r.out = "connected to 10.0.0.5:5555\n"; break;
            case 2: r.out = "Galaxy S23\n"; break; // not a Quest
            default: r.out = ""; break;
            }
            return r;
        }
    };

    FullSeqStub seq;
    SetupWizard wiz(seq);
    wiz.RunWifiDiscover();

    const StepResult r = wiz.RunWifiVerify();
    EXPECT_EQ(r.status, StepStatus::Failed);
    EXPECT_NE(r.detail.find("Galaxy S23"), std::string::npos);
}

// ---------------------------------------------------------------------------
// PolarityProbe_returns_matched
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, PolarityProbe_returns_matched)
{
    // StubAdb: SetGuardianPaused -> setprop (exit 0); GetGuardianPaused -> "1"
    class PolarityStub : public AdbController {
    public:
        int callIdx = 0;
        AdbController::Result Run(const std::vector<std::string>&,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            // Both setprop and getprop go through Shell -> Run.
            if (callIdx == 0) {
                r.out = "";   // setprop succeeds silently
            } else {
                r.out = "1\n"; // getprop returns 1
            }
            ++callIdx;
            return r;
        }
    };

    PolarityStub adb;
    const PolarityResult pr = ProbeGuardianPolarity(adb);

    EXPECT_EQ(pr.writtenValue, 1);
    EXPECT_EQ(pr.readBackValue, 1);
    EXPECT_TRUE(pr.readMatchesWrite);
}

// ---------------------------------------------------------------------------
// PolarityProbe_returns_mismatch
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, PolarityProbe_returns_mismatch)
{
    // setprop succeeds; getprop returns 0 (inverted firmware)
    class MismatchStub : public AdbController {
    public:
        int callIdx = 0;
        AdbController::Result Run(const std::vector<std::string>&,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            if (callIdx == 0) {
                r.out = ""; // setprop
            } else {
                r.out = "0\n"; // getprop returns opposite
            }
            ++callIdx;
            return r;
        }
    };

    MismatchStub adb;
    const PolarityResult pr = ProbeGuardianPolarity(adb);

    EXPECT_EQ(pr.writtenValue, 1);
    EXPECT_EQ(pr.readBackValue, 0);
    EXPECT_FALSE(pr.readMatchesWrite);
}

// ---------------------------------------------------------------------------
// IsDone_only_after_WifiVerify_passes
// ---------------------------------------------------------------------------
TEST(AdbSetupWizardTest, IsDone_only_after_WifiVerify_passes)
{
    class FullPassStub : public AdbController {
    public:
        int callIdx = 0;
        // Return true so CheckBinary does not short-circuit before calling Run.
        bool BinaryAvailable() const override { return true; }
        AdbController::Result Run(const std::vector<std::string>&,
                                  std::chrono::milliseconds) override
        {
            AdbController::Result r;
            r.exitCode = 0;
            switch (callIdx++) {
            // CheckBinary
            case 0: r.out = "Android Debug Bridge version 1.0.41\n"; break;
            // CheckDevAccount
            case 1: r.out = "List of devices attached\nABCD\tdevice\n"; break;
            // CheckDevMode
            case 2: r.out = "List of devices attached\nABCD\tdevice\n"; break;
            // UsbPair
            case 3: r.out = "List of devices attached\nABCD\tdevice\n"; break;
            // WifiTcpip
            case 4: r.out = "restarting in TCP mode port: 5555\n"; break;
            // WifiDiscover: ip route
            case 5: r.out = "192.168.1.0/24 dev wlan0 src 192.168.1.10\n"; break;
            // WifiVerify: connect
            case 6: r.out = "connected to 192.168.1.10:5555\n"; break;
            // WifiVerify: getprop
            case 7: r.out = "Quest 2\n"; break;
            default: r.out = ""; break;
            }
            return r;
        }
    };

    FullPassStub adb;
    SetupWizard wiz(adb);

    EXPECT_FALSE(wiz.IsDone());

    wiz.RunCheckBinary();
    wiz.RunCheckDevAccount();
    wiz.RunCheckDevMode();
    wiz.RunUsbPair();
    wiz.RunWifiTcpip();
    wiz.RunWifiDiscover();
    wiz.RunWifiVerify();

    EXPECT_TRUE(wiz.IsDone());
    EXPECT_EQ(wiz.DiscoveredEndpoint(), "192.168.1.10:5555");
}
