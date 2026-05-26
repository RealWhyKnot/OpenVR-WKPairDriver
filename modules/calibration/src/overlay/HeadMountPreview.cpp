#include "HeadMountPreview.h"

#include <openvr.h>
#include <Eigen/Geometry>

#include <cstring>
#include <cstdint>
#include <cstdio>

namespace wkopenvr::headmount {

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

namespace {

struct PreviewState {
    vr::VROverlayHandle_t handle  = vr::k_ulOverlayHandleInvalid;
    bool                  created = false;
};

PreviewState& PS() {
    static PreviewState s;
    return s;
}

constexpr const char* kPreviewKey   = "wkopenvr.headmount.preview";
constexpr const char* kPreviewName  = "Head-mount eye-position marker";
// Overlay diameter in metres. Small enough to be a distinct dot in the scene.
constexpr float kDotWidthM = 0.04f;

// Build a solid-color 8x8 RGBA texture (a flat colored disc). Called once on
// first creation. Color: bright cyan to stand out against most environments.
static void UploadDotTexture(vr::VROverlayHandle_t h) {
    constexpr int W = 8, H = 8;
    // Generate a circular mask so the marker looks like a dot, not a square.
    // RGBA, straight alpha.
    uint8_t pixels[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Normalised distance from centre [0..1] for a disc.
            float fx = (x + 0.5f) / W - 0.5f;
            float fy = (y + 0.5f) / H - 0.5f;
            float d  = std::sqrt(fx * fx + fy * fy) * 2.0f; // 0 at centre, 1 at rim
            uint8_t a = (d <= 1.0f) ? 255u : 0u;
            int idx = (y * W + x) * 4;
            pixels[idx + 0] = 0u;    // R
            pixels[idx + 1] = 230u;  // G
            pixels[idx + 2] = 255u;  // B
            pixels[idx + 3] = a;
        }
    }
    vr::VROverlay()->SetOverlayRaw(h, pixels, W, H, 4);
}

// Convert an Eigen AffineCompact3d world transform to an OpenVR HmdMatrix34.
// OpenVR matrices are row-major [3][4] (3 rows, 4 columns):
//   [ R00 R01 R02 Tx ]
//   [ R10 R11 R12 Ty ]
//   [ R20 R21 R22 Tz ]
static vr::HmdMatrix34_t ToHmdMatrix34(const Eigen::Matrix4d& m) {
    vr::HmdMatrix34_t out;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            out.m[r][c] = static_cast<float>(m(r, c));
        }
    }
    return out;
}

bool EnsureCreated() {
    auto& s = PS();
    if (s.created) return true;
    if (!vr::VROverlay()) return false;

    vr::EVROverlayError err =
        vr::VROverlay()->CreateOverlay(kPreviewKey, kPreviewName, &s.handle);
    if (err != vr::VROverlayError_None) {
        fprintf(stderr, "[HeadMountPreview] CreateOverlay failed: %d\n", (int)err);
        return false;
    }

    vr::VROverlay()->SetOverlayWidthInMeters(s.handle, kDotWidthM);
    // Sort order: above the average scene overlay so it's visible in passthrough.
    vr::VROverlay()->SetOverlaySortOrder(s.handle, 10);
    // Allow semitransparency.
    vr::VROverlay()->SetOverlayAlpha(s.handle, 1.0f);

    UploadDotTexture(s.handle);

    s.created = true;
    return true;
}

void Destroy() {
    auto& s = PS();
    if (!s.created) return;
    if (vr::VROverlay() && s.handle != vr::k_ulOverlayHandleInvalid) {
        vr::VROverlay()->DestroyOverlay(s.handle);
    }
    s.handle  = vr::k_ulOverlayHandleInvalid;
    s.created = false;
}

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void TickPreview(bool wantVisible,
                 const Eigen::Affine3d& headTrackerPose,
                 const Eigen::AffineCompact3d& headFromTracker)
{
    if (!wantVisible) {
        Destroy();
        return;
    }

    if (!EnsureCreated()) return;

    auto& s = PS();

    // Compute eye world transform: tracker_world * headFromTracker.
    Eigen::Matrix4d eyeWorld = (headTrackerPose *
        Eigen::Affine3d(headFromTracker)).matrix();
    vr::HmdMatrix34_t mat = ToHmdMatrix34(eyeWorld);

    // Place in raw tracking universe (same as driver-pose absolute space).
    vr::VROverlay()->SetOverlayTransformAbsolute(
        s.handle,
        vr::TrackingUniverseRawAndUncalibrated,
        &mat);

    vr::VROverlay()->ShowOverlay(s.handle);
}

} // namespace wkopenvr::headmount
