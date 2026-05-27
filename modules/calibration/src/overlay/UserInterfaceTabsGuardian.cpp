#include "UserInterfaceTabsGuardian.h"

#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "Configuration.h"
#include "DiagnosticsLog.h"
#include "GuardianAutoApply.h"
#include "SpaceCalibratorUmbrellaRuntime.h"
#include "UiHelpers.h"

#include <AdbSetupWizard.h>

#include <cstddef>
#include <cstdio>
#include <string>

extern CalibrationContext CalCtx;

void SaveProfile(CalibrationContext& ctx);

namespace {

bool s_showWizard = false;
bool s_awaitPolarityConfirm = false;
bool s_showAdbCleanupConfirm = false;
char s_wifiHostPort[64] = {};
char s_wifiCode[16] = {};
char s_wifiConnectEndpoint[64] = {};
std::string s_guardianError;
std::string s_adbCleanupStatus;
std::string s_adbReconnectStatus;
bool s_adbCleanupHadWarning = false;
bool s_adbReconnectHadWarning = false;

wkopenvr::adb::SetupWizard* WizardPtr()
{
    static wkopenvr::adb::SetupWizard inst(CCal_GetAdb());
    return &inst;
}

void RemoveAdbSetupFromQuest()
{
    AdbController& adb = CCal_GetAdb();
    const std::string endpoint = CalCtx.adb.savedEndpoint;
    const bool hadEndpoint = !endpoint.empty();

    bool connectOk = !hadEndpoint;
    if (hadEndpoint) {
        connectOk = adb.Connect(endpoint);
    }

    bool guardianCleared = false;
    if (connectOk) {
        guardianCleared = adb.SetGuardianPaused(false, 0);
    }

    const bool wirelessDisabled = adb.DisableWirelessAdb(endpoint);
    const bool disconnected = adb.Disconnect(endpoint);

    CalCtx.adb.setupCompleted = false;
    CalCtx.adb.savedEndpoint.clear();
    CalCtx.adb.guardianPauseEnabled = false;
    SaveProfile(CalCtx);

    const bool remoteConfirmed = (!hadEndpoint || connectOk) && guardianCleared && wirelessDisabled;
    s_adbCleanupHadWarning = !remoteConfirmed;
    if (remoteConfirmed) {
        s_adbCleanupStatus =
            "ADB setup removed. Quest Guardian was resumed, Wi-Fi ADB was switched back to USB mode, and the saved endpoint was cleared.";
    } else {
        s_adbCleanupStatus =
            "Local ADB setup was cleared, but the headset did not confirm every cleanup step. If you do not want ADB available, disable Developer Mode in the Meta Horizon app or headset settings.";
    }
    s_guardianError.clear();
    Metrics::adbConnected.Push(false);
    Metrics::guardianPaused.Push(false);

    std::fprintf(stderr,
        "[adb-ui] remove setup: endpoint='%s' connect=%d guardian_clear=%d usb=%d disconnect=%d remote_confirmed=%d\n",
        endpoint.c_str(), connectOk ? 1 : 0, guardianCleared ? 1 : 0,
        wirelessDisabled ? 1 : 0, disconnected ? 1 : 0, remoteConfirmed ? 1 : 0);
}

void DrawAdbCleanupConfirmModal()
{
    if (!s_showAdbCleanupConfirm) return;

    ImGui::SetNextWindowSize(ImVec2(680.0f, 300.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    if (ImGui::BeginPopupModal("Remove ADB setup##adb_cleanup",
                               &s_showAdbCleanupConfirm, 0)) {
        const auto& pal = openvr_pair::overlay::ui::GetPalette();
        const float actionHeight = ImGui::GetTextLineHeightWithSpacing() * 1.35f;
        const ImVec2 actionButtonSize(250.0f, actionHeight);

        openvr_pair::overlay::ui::DrawBanner(
            "Remove ADB setup",
            "This resumes Quest Guardian, asks the headset to leave Wi-Fi ADB mode, disconnects the saved endpoint, and clears WKOpenVR's saved ADB setup. No app is installed by this feature.",
            pal.bannerWarnBg, pal.bannerWarnTitle, pal.bannerWarnDetail);
        ImGui::Spacing();

        if (ImGui::Button("Remove setup##confirm_adb_cleanup", actionButtonSize)) {
            RemoveAdbSetupFromQuest();
            s_showAdbCleanupConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##cancel_adb_cleanup", actionButtonSize)) {
            s_showAdbCleanupConfirm = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void TryReconnectSavedEndpointFromUi()
{
    const auto result = wkopenvr::adb::ReconnectSavedEndpoint(
        CCal_GetAdb(),
        CalCtx.adb.guardianPauseEnabled);

    if (!result.endpointPresent) {
        s_adbReconnectHadWarning = true;
        s_adbReconnectStatus = "No saved endpoint is available yet. Run ADB setup once first.";
        return;
    }

    if (!result.connected) {
        s_adbReconnectHadWarning = true;
        s_adbReconnectStatus =
            "Saved endpoint did not answer. If the headset rebooted, Quest usually leaves classic Wi-Fi ADB mode; plug in USB and re-run setup once to enable Wi-Fi ADB again.";
        return;
    }

    CalCtx.adb.setupCompleted = true;
    SaveProfile(CalCtx);
    s_guardianError.clear();
    s_adbReconnectHadWarning = false;

    if (result.reapplyAttempted && !result.reapplyConfirmed) {
        s_adbReconnectHadWarning = true;
        s_adbReconnectStatus =
            "Reconnected to the saved endpoint, but Guardian pause did not confirm. Try Pause Quest Guardian again or re-run setup.";
    } else if (result.reapplyConfirmed) {
        s_adbReconnectStatus =
            "Reconnected to the saved endpoint and re-applied the Guardian pause.";
    } else {
        s_adbReconnectStatus = "Reconnected to the saved endpoint.";
    }
}

} // namespace

void CCal_DrawSetupWizardModal()
{
    if (!s_showWizard) return;

    ImGui::SetNextWindowSize(ImVec2(980.0f, 740.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    if (ImGui::BeginPopupModal("Connect to Quest##wiz", &s_showWizard, 0)) {
        ImGui::SetWindowFontScale(1.22f);

        auto* wiz = WizardPtr();
        const auto& pal = openvr_pair::overlay::ui::GetPalette();
        const float actionHeight = ImGui::GetTextLineHeightWithSpacing() * 1.45f;
        const ImVec2 actionButtonSize(360.0f, actionHeight);
        auto ActionButton = [&](const char* label) {
            return ImGui::Button(label, actionButtonSize);
        };
        auto FullWidthInputText = [](const char* visibleLabel, const char* id, char* buffer, size_t bufferSize) {
            ImGui::TextWrapped("%s", visibleLabel);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            return ImGui::InputText(id, buffer, bufferSize);
        };

        ImGui::BeginChild("adb_setup_scroll_region", ImVec2(0.0f, 0.0f), false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        openvr_pair::overlay::ui::DrawBanner(
            "ADB warning",
            "ADB authorization gives this PC debugging access to the Quest while it is trusted. Continue only with your own unlocked headset, and remove ADB setup when you are done if you do not want Wi-Fi debugging left available.",
            pal.bannerWarnBg, pal.bannerWarnTitle, pal.bannerWarnDetail);
        ImGui::Spacing();

        struct StepInfo { wkopenvr::adb::WizardStep step; const char* label; const char* help; };
        static const StepInfo kSteps[] = {
            { wkopenvr::adb::WizardStep::CheckBinary,    "ADB binary",
              "Verify the bundled adb.exe can run." },
            { wkopenvr::adb::WizardStep::CheckDevAccount,"Meta developer account",
              "In the Meta Horizon app, open headset settings and turn Developer Mode on." },
            { wkopenvr::adb::WizardStep::CheckDevMode,   "USB authorization",
              "Connect a USB-C data cable, unlock the headset, then accept 'Allow USB debugging?'. MTP Notification is only the file-transfer notification; it does not authorize ADB." },
            { wkopenvr::adb::WizardStep::UsbPair,        "USB pairing",
              "Confirm the Quest is paired over USB." },
            { wkopenvr::adb::WizardStep::WifiTcpip,      "Enable Wi-Fi ADB",
              "Switch the Quest into Wi-Fi ADB mode (adb tcpip 5555)." },
            { wkopenvr::adb::WizardStep::WifiDiscover,   "Discover Quest IP",
              "Read the Quest's Wi-Fi IP via the USB connection. This normal Quest path does not need a pairing code." },
            { wkopenvr::adb::WizardStep::WifiVerify,     "Verify Wi-Fi",
              "Connect over Wi-Fi and probe the Guardian property." },
        };
        static const StepInfo kManualWirelessSteps[] = {
            { wkopenvr::adb::WizardStep::WifiPair,       "Manual wireless pair",
              "Only use this if the Quest shows an Android Wireless debugging pairing-code screen. Many Quest builds do not expose it; the USB-authorized path above is the expected flow." },
            { wkopenvr::adb::WizardStep::WifiVerify,     "Verify Wi-Fi",
              "Connect over Wi-Fi and probe the Guardian property." },
        };

        const wkopenvr::adb::WizardStep cur = wiz->currentStep();
        const bool manualWirelessFlow =
            cur == wkopenvr::adb::WizardStep::WifiPair ||
            (cur == wkopenvr::adb::WizardStep::WifiVerify && wiz->DiscoveredEndpoint().empty());
        const StepInfo* steps = manualWirelessFlow ? kManualWirelessSteps : kSteps;
        const int stepCount = manualWirelessFlow
            ? (int)(sizeof kManualWirelessSteps / sizeof kManualWirelessSteps[0])
            : (int)(sizeof kSteps / sizeof kSteps[0]);
        int curIdx = 0;
        for (int i = 0; i < stepCount; ++i) {
            if (steps[i].step == cur) { curIdx = i; break; }
        }

        if (!wiz->IsDone()) {
            ImGui::TextWrapped("Step %d of %d - %s", curIdx + 1, stepCount, steps[curIdx].label);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", steps[curIdx].help);
            ImGui::Spacing();

            if (cur == wkopenvr::adb::WizardStep::WifiTcpip ||
                cur == wkopenvr::adb::WizardStep::WifiPair ||
                cur == wkopenvr::adb::WizardStep::WifiVerify) {
                openvr_pair::overlay::ui::DrawBanner(
                    "Wireless ADB",
                    "Wireless ADB opens a debugging endpoint on the headset's network. Use Remove ADB setup from the Guardian panel when finished, or disable Developer Mode in Meta settings.",
                    pal.bannerWarnBg, pal.bannerWarnTitle, pal.bannerWarnDetail);
                ImGui::Spacing();
            }

            switch (cur) {
            case wkopenvr::adb::WizardStep::Start:
            case wkopenvr::adb::WizardStep::CheckBinary:
                if (ActionButton("Check##binary")) wiz->RunCheckBinary();
                if (Metrics::adbConnected.last()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("ADB is already connected.");
                    if (ActionButton("Use current ADB connection##use_current_adb")) {
                        wiz->UseCurrentAdbConnection();
                    }
                }
                break;
            case wkopenvr::adb::WizardStep::CheckDevAccount:
                if (ActionButton("Check##devacct")) wiz->RunCheckDevAccount();
                break;
            case wkopenvr::adb::WizardStep::CheckDevMode:
                if (ActionButton("Check##devmode")) wiz->RunCheckDevMode();
                break;
            case wkopenvr::adb::WizardStep::UsbPair:
                if (ActionButton("Confirm##usbpair")) wiz->RunUsbPair();
                break;
            case wkopenvr::adb::WizardStep::WifiTcpip:
                if (ActionButton("Enable Wi-Fi ADB##tcpip")) wiz->RunWifiTcpip();
                break;
            case wkopenvr::adb::WizardStep::WifiDiscover:
                if (ActionButton("Discover##disc")) wiz->RunWifiDiscover();
                break;
            case wkopenvr::adb::WizardStep::WifiPair:
                FullWidthInputText("Pairing host:port", "##wiz_hp",
                    s_wifiHostPort, sizeof(s_wifiHostPort));
                ImGui::TextWrapped("Pairing code");
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputText("##wiz_code", s_wifiCode, sizeof(s_wifiCode));
                if (wiz->DiscoveredEndpoint().empty()) {
                    FullWidthInputText("Connect endpoint", "##wiz_ep",
                        s_wifiConnectEndpoint, sizeof(s_wifiConnectEndpoint));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("The endpoint from the Wireless ADB screen, usually IP:port. This is not always the same port as the pairing host:port.");
                    }
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("Connect endpoint: %s", wiz->DiscoveredEndpoint().c_str());
                    ImGui::PopStyleColor();
                }
                if (ActionButton("Pair##pair")) {
                    wiz->RunWifiPair(s_wifiHostPort, s_wifiCode);
                }
                break;
            case wkopenvr::adb::WizardStep::WifiVerify:
                if (wiz->DiscoveredEndpoint().empty()) {
                    FullWidthInputText("Connect endpoint", "##wiz_verify_ep",
                        s_wifiConnectEndpoint, sizeof(s_wifiConnectEndpoint));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("Connect endpoint: %s", wiz->DiscoveredEndpoint().c_str());
                    ImGui::PopStyleColor();
                }
                if (ActionButton("Verify##verify")) {
                    const std::string manualEndpoint =
                        wiz->DiscoveredEndpoint().empty()
                            ? std::string(s_wifiConnectEndpoint)
                            : std::string();
                    auto res = wiz->RunWifiVerify(manualEndpoint);
                    if (res.status == wkopenvr::adb::StepStatus::Passed) {
                        const auto polarity =
                            wkopenvr::adb::ProbeGuardianPolarity(CCal_GetAdb());
                        {
                            char pbuf[160];
                            std::snprintf(pbuf, sizeof pbuf,
                                "[adb-wizard-ui] guardian probe complete: wrote=%d read=%d match=%d",
                                polarity.writtenValue,
                                polarity.readBackValue,
                                polarity.readMatchesWrite ? 1 : 0);
                            Metrics::WriteLogAnnotation(pbuf);
                            openvr_pair::common::DiagnosticLog("adb-wizard-ui", "%s", pbuf);
                        }
                        const std::string ep = wiz->DiscoveredEndpoint();
                        if (!ep.empty()) {
                            CalCtx.adb.savedEndpoint = ep;
                            SaveProfile(CalCtx);
                        }
                        s_awaitPolarityConfirm = true;
                    }
                }
                break;
            default:
                break;
            }

            const auto res = wiz->stepResult(cur);
            if (res.status == wkopenvr::adb::StepStatus::Failed && !res.detail.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, pal.statusError);
                ImGui::TextWrapped("%s", res.detail.c_str());
                ImGui::PopStyleColor();
            }
            if ((cur == wkopenvr::adb::WizardStep::CheckDevMode ||
                 cur == wkopenvr::adb::WizardStep::UsbPair) &&
                res.status == wkopenvr::adb::StepStatus::Failed) {
                ImGui::Spacing();
                ImGui::TextDisabled("No USB prompt?");
                ImGui::TextWrapped("Retry restarts the ADB server and checks the headset state. The manual wireless fallback only applies if the headset shows Android Wireless debugging pairing details.");
                if (ImGui::Button("Try to show USB prompt##retry_usb", actionButtonSize)) {
                    wiz->RunRetryUsbPrompt();
                }
                if (ImGui::GetContentRegionAvail().x >= actionButtonSize.x + ImGui::GetStyle().ItemSpacing.x) {
                    ImGui::SameLine();
                }
                if (ImGui::Button("Manual pairing-code fallback##wireless_fallback", actionButtonSize)) {
                    wiz->UseWirelessFallback();
                }
            }
        } else if (s_awaitPolarityConfirm) {
            ImGui::TextWrapped("Did Quest Guardian visibly disappear in-headset just now?");
            ImGui::Spacing();
            if (ImGui::Button("Yes, Guardian disappeared", actionButtonSize)) {
                Metrics::WriteLogAnnotation("[adb-wizard-ui] guardian confirmation: manual_confirmed_disappeared");
                openvr_pair::common::DiagnosticLog(
                    "adb-wizard-ui", "guardian confirmation manual_confirmed_disappeared value=%d",
                    CalCtx.adb.guardianPauseValue);
                wkopenvr::adb::RecordGuardianPausedConfirmation("wizard_probe");
                CalCtx.adb.setupCompleted = true;
                SaveProfile(CalCtx);
                s_awaitPolarityConfirm = false;
                s_showWizard = false;
            }
            if (ImGui::GetContentRegionAvail().x >= actionButtonSize.x + ImGui::GetStyle().ItemSpacing.x) {
                ImGui::SameLine();
            }
            if (ImGui::Button("No, flip the value", actionButtonSize)) {
                Metrics::WriteLogAnnotation("[adb-wizard-ui] guardian confirmation: manual_requested_flip");
                openvr_pair::common::DiagnosticLog(
                    "adb-wizard-ui", "guardian confirmation manual_requested_flip old_value=%d new_value=%d",
                    CalCtx.adb.guardianPauseValue,
                    CalCtx.adb.guardianPauseValue == 1 ? 0 : 1);
                const bool confirmed = wkopenvr::adb::SetGuardianPauseValueOverride(CCal_GetAdb(),
                    CalCtx.adb.guardianPauseValue == 1 ? 0 : 1);
                if (confirmed) {
                    wkopenvr::adb::RecordGuardianPausedConfirmation("wizard_flip");
                    CalCtx.adb.setupCompleted = true;
                    SaveProfile(CalCtx);
                    s_awaitPolarityConfirm = false;
                    s_showWizard = false;
                } else {
                    s_guardianError = "Guardian did not confirm after flipping the pause value.";
                }
            }
        } else {
            ImGui::TextColored(pal.statusOk, "Setup complete.");
            if (ImGui::Button("Close", actionButtonSize)) { s_showWizard = false; }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::TreeNode("Show all steps")) {
            for (const auto& si : kSteps) {
                const auto res = wiz->stepResult(si.step);
                switch (res.status) {
                case wkopenvr::adb::StepStatus::Passed:
                    ImGui::TextColored(pal.statusOk, "[+] %s", si.label);
                    break;
                case wkopenvr::adb::StepStatus::Failed:
                    ImGui::TextColored(pal.statusError, "[x] %s", si.label);
                    break;
                case wkopenvr::adb::StepStatus::InProgress:
                    ImGui::TextColored(pal.statusWarn, "[~] %s", si.label);
                    break;
                default:
                    ImGui::TextDisabled("[ ] %s", si.label);
                    break;
                }
                if (!res.detail.empty()) {
                    ImGui::Indent();
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s", res.detail.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Unindent();
                }
            }
            ImGui::TreePop();
        }

        if (!wiz->IsDone()) {
            ImGui::Spacing();
            if (ImGui::Button("Reset", ImVec2(120.0f, actionHeight * 0.85f))) {
                wiz->Reset();
                s_wifiHostPort[0] = '\0';
                s_wifiCode[0] = '\0';
                s_wifiConnectEndpoint[0] = '\0';
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
}

void CCal_DrawGuardianSection(ImVec2 panelSize)
{
    openvr_pair::overlay::ui::PanelScope panel("Step 3: Quest Guardian", panelSize);
    const auto& pal = openvr_pair::overlay::ui::GetPalette();

    ImGui::TextWrapped(
        "Pause Quest's own Guardian so it doesn't fight your drawn boundary in-headset.");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Requires ADB connectivity to the Quest. Guardian is paused via a Meta\n"
            "runtime property; nothing is installed on the headset. Re-enable any time.");
    }
    ImGui::Spacing();

    const bool adbConn = Metrics::adbConnected.last();
    const bool guardianPaused = Metrics::guardianPaused.last();
    const bool hasBoundary = CalCtx.boundary.enabled && CalCtx.boundary.vertices.size() >= 3;

    openvr_pair::overlay::ui::DrawStatusDot(adbConn ? pal.dotOk : pal.dotError);
    ImGui::TextUnformatted(adbConn ? "ADB connected" : "ADB not connected");
    if (!adbConn && !CalCtx.adb.savedEndpoint.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(saved endpoint: %s)", CalCtx.adb.savedEndpoint.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  |  ");
    ImGui::SameLine();
    if (!adbConn) {
        openvr_pair::overlay::ui::DrawStatusDot(pal.dotPending);
        ImGui::TextDisabled("Guardian: unknown");
    } else if (guardianPaused) {
        openvr_pair::overlay::ui::DrawStatusDot(pal.dotOk);
        ImGui::TextUnformatted("Guardian: paused");
    } else {
        openvr_pair::overlay::ui::DrawStatusDot(pal.dotPending);
        ImGui::TextUnformatted("Guardian: active");
    }

    ImGui::Spacing();

    if (!adbConn && !CalCtx.adb.savedEndpoint.empty()) {
        if (ImGui::Button("Reconnect saved endpoint##adb_reconnect_saved")) {
            TryReconnectSavedEndpointFromUi();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Try the saved Wi-Fi ADB endpoint without repeating the USB setup. If the Quest rebooted, Wi-Fi ADB may need to be enabled over USB again.");
        }
        ImGui::SameLine();
    }

    if (!CalCtx.adb.setupCompleted || !adbConn) {
        if (ImGui::Button("Connect to Quest via ADB...")) {
            WizardPtr()->Reset();
            s_awaitPolarityConfirm = false;
            s_showWizard = true;
            ImGui::OpenPopup("Connect to Quest##wiz");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Walks through Meta developer-mode + USB + Wi-Fi pairing.");
        }
        if (CalCtx.adb.setupCompleted && !adbConn) {
            ImGui::SameLine();
            ImGui::TextDisabled("(setup ran previously; reconnect to retry)");
        }
    } else if (!hasBoundary) {
        ImGui::TextDisabled("Draw and enable a boundary above before pausing Guardian.");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Pausing Quest's Guardian without a replacement boundary is a safety\n"
                "regression. WKOpenVR refuses to do this until the Safety boundary\n"
                "above is active.");
        }
    } else if (!guardianPaused) {
        if (ImGui::Button("Pause Quest Guardian")) {
            if (!wkopenvr::adb::ApplyGuardianPauseSetting(CCal_GetAdb(), true)) {
                s_guardianError = "Failed to pause Guardian. Check ADB connection.";
            } else {
                CalCtx.adb.guardianPauseEnabled = true;
                SaveProfile(CalCtx);
                s_guardianError.clear();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sets debug.oculus.guardian_pause on the Quest so Guardian disappears in-headset.");
        }
    } else {
        if (ImGui::Button("Resume Quest Guardian")) {
            if (!wkopenvr::adb::ApplyGuardianPauseSetting(CCal_GetAdb(), false)) {
                s_guardianError = "Failed to resume Guardian.";
            } else {
                CalCtx.adb.guardianPauseEnabled = false;
                SaveProfile(CalCtx);
                s_guardianError.clear();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clears the pause property so Guardian becomes visible in-headset again.");
        }
    }

    if (CalCtx.adb.setupCompleted) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Re-run setup##rerun")) {
            WizardPtr()->Reset();
            s_awaitPolarityConfirm = false;
            s_showWizard = true;
            ImGui::OpenPopup("Connect to Quest##wiz");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Walk through the ADB pairing again, e.g. after a Wi-Fi change.");
        }
    }

    if (CalCtx.adb.setupCompleted || !CalCtx.adb.savedEndpoint.empty() ||
        CalCtx.adb.guardianPauseEnabled) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove ADB setup##remove_adb_setup")) {
            s_showAdbCleanupConfirm = true;
            ImGui::OpenPopup("Remove ADB setup##adb_cleanup");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Resume Guardian, turn off Wi-Fi ADB when reachable, disconnect the saved endpoint, and clear the saved setup.");
        }
    }

    if (!s_adbCleanupStatus.empty()) {
        ImGui::Spacing();
        if (s_adbCleanupHadWarning) {
            openvr_pair::overlay::ui::DrawBanner(
                "ADB cleanup", s_adbCleanupStatus.c_str(),
                pal.bannerWarnBg, pal.bannerWarnTitle, pal.bannerWarnDetail);
        } else {
            openvr_pair::overlay::ui::DrawInfoBanner("ADB cleanup", s_adbCleanupStatus.c_str());
        }
    }

    if (!s_adbReconnectStatus.empty()) {
        ImGui::Spacing();
        if (s_adbReconnectHadWarning) {
            openvr_pair::overlay::ui::DrawBanner(
                "ADB reconnect", s_adbReconnectStatus.c_str(),
                pal.bannerWarnBg, pal.bannerWarnTitle, pal.bannerWarnDetail);
        } else {
            openvr_pair::overlay::ui::DrawInfoBanner("ADB reconnect", s_adbReconnectStatus.c_str());
        }
    }

    if (!s_guardianError.empty()) {
        ImGui::Spacing();
        openvr_pair::overlay::ui::DrawErrorBanner("Guardian error", s_guardianError.c_str());
    }

    DrawAdbCleanupConfirmModal();
}
