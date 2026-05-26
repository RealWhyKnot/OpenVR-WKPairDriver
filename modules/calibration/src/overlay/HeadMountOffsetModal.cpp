#include "HeadMountOffsetModal.h"

#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "HeadFromTrackerSolve.h"
#include "HeadMountPreview.h"

#include <imgui/imgui.h>
#include <Eigen/Geometry>

#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>

// SaveProfile is defined in Calibration.cpp; expose via forward declaration
// matching the pattern used by Wizard.cpp.
void SaveProfile(CalibrationContext& ctx);

namespace wkopenvr::headmount {

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

namespace {

struct ModalState {
    bool        wantOpen    = false;
    bool        popupOpened = false;
    Solver      solver;
    SolveResult lastResult;     // copy of solver.result() at the moment Finish ran
    bool        showResult  = false;  // true while awaiting Save/Discard after Done/Failed
};

ModalState& MS() {
    static ModalState s;
    return s;
}

constexpr const char* kPopupId = "##hmt_offset_modal";

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void OpenOffsetModal() {
    auto& s = MS();
    // Reset state but keep the solver clean until the user hits Start.
    s.solver.Cancel();
    s.showResult  = false;
    s.popupOpened = false;
    s.wantOpen    = true;
}

bool OffsetModalIsOpen() {
    return MS().popupOpened || MS().wantOpen;
}

bool DrawOffsetModal() {
    auto& s = MS();

    if (s.wantOpen && !s.popupOpened) {
        ImGui::OpenPopup(kPopupId);
        s.popupOpened = true;
        s.wantOpen    = false;
    }

    if (!s.popupOpened) return false;

    ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(
        ImVec2((vpSize.x - 480.0f) * 0.5f, (vpSize.y - 260.0f) * 0.5f),
        ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 260.0f), ImGuiCond_Always);

    bool savedOffset = false;

    if (ImGui::BeginPopupModal(kPopupId, nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize   |
            ImGuiWindowFlags_NoMove))
    {
        ImGui::TextUnformatted("Head-Tracker Offset Calibration");
        ImGui::Separator();
        ImGui::Spacing();

        const SolveState st = s.solver.state();

        if (!s.showResult) {
            // --- Idle / collecting phase ---
            if (st == SolveState::Idle) {
                ImGui::TextWrapped(
                    "Attach the tracker firmly to your headset. "
                    "Click Start, then move your head slowly through "
                    "pitch, yaw, and roll for ~10 seconds.");
                ImGui::Spacing();
                if (ImGui::Button("Start##hmt_start")) {
                    s.solver.Start();
                    Metrics::WriteLogAnnotation("[head-mount-modal] solver started");
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##hmt_cancel_idle")) {
                    ImGui::CloseCurrentPopup();
                    s.popupOpened = false;
                }
            }
            else if (st == SolveState::Collecting) {
                const size_t collected = s.solver.sampleCount();
                const float  fraction  = static_cast<float>(collected) /
                    static_cast<float>(Solver::kTargetSampleCount);
                char progLabel[64];
                std::snprintf(progLabel, sizeof progLabel,
                    "%zu / %zu samples", collected, Solver::kTargetSampleCount);
                ImGui::ProgressBar(fraction > 1.0f ? 1.0f : fraction,
                    ImVec2(-1.0f, 0.0f), progLabel);
                ImGui::Spacing();
                ImGui::TextUnformatted("Move head slowly through pitch, yaw, and roll...");
                ImGui::Spacing();

                bool canFinish = collected >= Solver::kTargetSampleCount;
                if (!canFinish) ImGui::BeginDisabled();
                if (ImGui::Button("Finish##hmt_finish")) {
                    s.solver.Finish();
                    s.lastResult  = s.solver.result();
                    s.showResult  = true;
                    // Push solve residual so the diagnostics graph shows per-solve
                    // accuracy. Only push on success; failure residual is noisy.
                    if (s.lastResult.failReason.empty()) {
                        Metrics::headMountResidualMm.Push(s.lastResult.residualMm);
                        char rbuf[128];
                        snprintf(rbuf, sizeof rbuf,
                            "[head-mount-modal] solved: residual=%.2fmm samples=%d",
                            s.lastResult.residualMm, s.lastResult.samplesUsed);
                        Metrics::WriteLogAnnotation(rbuf);
                    } else {
                        char rbuf[256];
                        snprintf(rbuf, sizeof rbuf,
                            "[head-mount-modal] failed: reason='%s' residual=%.2fmm samples=%d",
                            s.lastResult.failReason.c_str(),
                            s.lastResult.residualMm, s.lastResult.samplesUsed);
                        Metrics::WriteLogAnnotation(rbuf);
                    }
                    {
                        char fbuf[96];
                        snprintf(fbuf, sizeof fbuf,
                            "[head-mount-modal] finish requested: samples=%zu",
                            collected);
                        Metrics::WriteLogAnnotation(fbuf);
                    }
                }
                if (!canFinish) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel##hmt_cancel_coll")) {
                    s.solver.Cancel();
                    ImGui::CloseCurrentPopup();
                    s.popupOpened = false;
                }
            }
        }
        else {
            // --- Result phase (Done or Failed) ---
            const bool success = (s.lastResult.failReason.empty());
            if (success) {
                char buf[128];
                std::snprintf(buf, sizeof buf,
                    "Solved: residual %.2f mm  (%d samples)",
                    s.lastResult.residualMm,
                    s.lastResult.samplesUsed);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                ImGui::TextUnformatted(buf);
                ImGui::PopStyleColor();
                ImGui::Spacing();
                if (ImGui::Button("Save##hmt_save")) {
                    CalCtx.headMount.headFromTracker = s.lastResult.headFromTracker;
                    CalCtx.headMount.offsetCalibrated = true;
                    SaveProfile(CalCtx);
                    {
                        const Eigen::Vector3d tcm = s.lastResult.headFromTracker.translation() * 100.0;
                        const Eigen::Vector3d rpy = s.lastResult.headFromTracker.rotation()
                            .eulerAngles(2, 1, 0) * (180.0 / EIGEN_PI);
                        char sbuf[192];
                        snprintf(sbuf, sizeof sbuf,
                            "[head-mount-modal] offset saved:"
                            " trans=(%.2f,%.2f,%.2f)cm rpy=(%.2f,%.2f,%.2f)deg",
                            tcm.x(), tcm.y(), tcm.z(),
                            rpy(0), rpy(1), rpy(2));
                        Metrics::WriteLogAnnotation(sbuf);
                    }
                    savedOffset   = true;
                    s.showResult  = false;
                    s.solver.Cancel();
                    ImGui::CloseCurrentPopup();
                    s.popupOpened = false;
                }
                ImGui::SameLine();
            }
            else {
                char buf[256];
                std::snprintf(buf, sizeof buf,
                    "Solve failed: %s", s.lastResult.failReason.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextUnformatted(buf);
                ImGui::PopStyleColor();
                ImGui::Spacing();
                // Let the user retry from scratch.
                if (ImGui::Button("Retry##hmt_retry")) {
                    s.solver.Cancel();
                    s.showResult = false;
                }
                ImGui::SameLine();
            }

            if (ImGui::Button("Discard##hmt_discard")) {
                s.showResult  = false;
                s.solver.Cancel();
                ImGui::CloseCurrentPopup();
                s.popupOpened = false;
            }
        }

        ImGui::EndPopup();
    }
    else {
        // ImGui auto-closed (Esc or outside click) -- sync state.
        if (s.popupOpened) {
            s.popupOpened = false;
            s.showResult  = false;
            s.solver.Cancel();
        }
    }

    // Drive the live preview marker while the modal is open.
    {
        const bool modalOpen = s.popupOpened;
        const auto& hm = CalCtx.headMount;
        bool trackerOk = hm.deviceID >= 0
            && (uint32_t)hm.deviceID < vr::k_unMaxTrackedDeviceCount
            && CalCtx.devicePoses[hm.deviceID].poseIsValid;

        if (modalOpen && trackerOk) {
            const vr::DriverPose_t& raw = CalCtx.devicePoses[hm.deviceID];
            // Build world pose from DriverPose fields.
            Eigen::Quaterniond wfd(
                raw.qWorldFromDriverRotation.w,
                raw.qWorldFromDriverRotation.x,
                raw.qWorldFromDriverRotation.y,
                raw.qWorldFromDriverRotation.z);
            Eigen::Quaterniond localRot(
                raw.qRotation.w, raw.qRotation.x,
                raw.qRotation.y, raw.qRotation.z);
            Eigen::Quaterniond worldRot = (wfd * localRot).normalized();
            Eigen::Vector3d wfdTrans(
                raw.vecWorldFromDriverTranslation[0],
                raw.vecWorldFromDriverTranslation[1],
                raw.vecWorldFromDriverTranslation[2]);
            Eigen::Vector3d localPos(
                raw.vecPosition[0], raw.vecPosition[1], raw.vecPosition[2]);
            Eigen::Affine3d trackerWorld =
                Eigen::Translation3d(wfdTrans + wfd * localPos) * worldRot;

            TickPreview(true, trackerWorld, hm.headFromTracker);
        }
        else {
            TickPreview(false, Eigen::Affine3d::Identity(),
                Eigen::AffineCompact3d::Identity());
        }
    }

    return savedOffset;
}

// ---------------------------------------------------------------------------
// Inline nudge-slider panel
// ---------------------------------------------------------------------------

void DrawOffsetInlinePanel() {
    auto& hm = CalCtx.headMount;

    // Decompose headFromTracker into XYZ (cm) + RPY (deg) for display.
    // The sliders write back into the transform each frame.
    Eigen::Vector3d t = hm.headFromTracker.translation() * 100.0; // m -> cm
    Eigen::Vector3d rpy = hm.headFromTracker.rotation()
        .eulerAngles(2, 1, 0) * (180.0 / EIGEN_PI);              // ZYX -> yaw/pitch/roll in deg

    bool changed = false;

    auto SliderCm = [&](const char* label, double& v) -> bool {
        float fv = static_cast<float>(v);
        if (ImGui::SliderFloat(label, &fv, -5.0f, 5.0f, "%.2f cm",
                ImGuiSliderFlags_AlwaysClamp)) {
            v = static_cast<double>(fv);
            return true;
        }
        return false;
    };
    auto SliderDeg = [&](const char* label, double& v) -> bool {
        float fv = static_cast<float>(v);
        if (ImGui::SliderFloat(label, &fv, -15.0f, 15.0f, "%.2f deg",
                ImGuiSliderFlags_AlwaysClamp)) {
            v = static_cast<double>(fv);
            return true;
        }
        return false;
    };

    if (hm.offsetCalibrated) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "Residual from last solve: calibrated");
        ImGui::TextUnformatted(buf);
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
        ImGui::TextUnformatted("Offset not calibrated -- use the Calibrate button above.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Fine adjustment (XYZ offset, RPY rotation):");
    ImGui::Spacing();

    changed |= SliderCm("X##hft_x", t(0));
    changed |= SliderCm("Y##hft_y", t(1));
    changed |= SliderCm("Z##hft_z", t(2));
    changed |= SliderDeg("Yaw##hft_yaw",   rpy(0));
    changed |= SliderDeg("Pitch##hft_pit", rpy(1));
    changed |= SliderDeg("Roll##hft_rol",  rpy(2));

    if (changed) {
        // Recompose. ZYX order matches eulerAngles(2,1,0) decomposition above.
        Eigen::Quaterniond q =
            Eigen::AngleAxisd(rpy(0) * EIGEN_PI / 180.0, Eigen::Vector3d::UnitZ()) *
            Eigen::AngleAxisd(rpy(1) * EIGEN_PI / 180.0, Eigen::Vector3d::UnitY()) *
            Eigen::AngleAxisd(rpy(2) * EIGEN_PI / 180.0, Eigen::Vector3d::UnitX());
        hm.headFromTracker.linear()      = q.toRotationMatrix();
        hm.headFromTracker.translation() = t * 0.01; // cm -> m

        // Drive preview while sliders are active.
        bool trackerOk = hm.deviceID >= 0
            && (uint32_t)hm.deviceID < vr::k_unMaxTrackedDeviceCount
            && CalCtx.devicePoses[hm.deviceID].poseIsValid;
        if (trackerOk) {
            const vr::DriverPose_t& raw = CalCtx.devicePoses[hm.deviceID];
            Eigen::Quaterniond wfd(
                raw.qWorldFromDriverRotation.w,
                raw.qWorldFromDriverRotation.x,
                raw.qWorldFromDriverRotation.y,
                raw.qWorldFromDriverRotation.z);
            Eigen::Quaterniond localRot(
                raw.qRotation.w, raw.qRotation.x,
                raw.qRotation.y, raw.qRotation.z);
            Eigen::Affine3d trackerWorld =
                Eigen::Translation3d(
                    Eigen::Vector3d(
                        raw.vecWorldFromDriverTranslation[0],
                        raw.vecWorldFromDriverTranslation[1],
                        raw.vecWorldFromDriverTranslation[2]) +
                    wfd *
                    Eigen::Vector3d(
                        raw.vecPosition[0],
                        raw.vecPosition[1],
                        raw.vecPosition[2])) *
                (wfd * localRot).normalized();
            TickPreview(true, trackerWorld, hm.headFromTracker);
        }
    }
}

// Called from HeadMountPoseSampling tick to feed the modal's Solver.
// The function signature matches what CalibrationTick can call after building
// the world-space poses from DriverPose_t fields.
void FeedSolverTick(const Eigen::Affine3d& hmdPose,
                    const Eigen::Affine3d& trackerPose,
                    double hmdSpeedMps)
{
    const bool accepted = MS().solver.Tick(hmdPose, trackerPose, hmdSpeedMps);

    // Rate-limited rejection log: count rejections in the current 1 s window and
    // emit one annotation per second when any rejections occurred.
    if (MS().solver.state() == SolveState::Collecting && !accepted) {
        static int    s_rejCount    = 0;
        static double s_windowStart = 0.0;
        const double  now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (s_windowStart == 0.0) s_windowStart = now;
        ++s_rejCount;
        if (now - s_windowStart >= 1.0) {
            char rbuf[128];
            snprintf(rbuf, sizeof rbuf,
                "[head-mount-modal] rejecting samples:"
                " hmdSpeed below threshold (%d rejections last 1s)",
                s_rejCount);
            Metrics::WriteLogAnnotation(rbuf);
            s_rejCount    = 0;
            s_windowStart = now;
        }
    }
}

} // namespace wkopenvr::headmount
