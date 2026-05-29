#include "BoundaryPreview.h"

#include "Boundary.h"
#include "DiagnosticsLog.h"

#include <openvr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace wkopenvr::boundary {
namespace {

constexpr double kMinPreviewSpanMeters = 1.0;
constexpr double kPreviewPadMeters = 0.30;
constexpr int kUploadFailureDisableThreshold = 3;
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

struct PreviewState {
    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    bool created = false;
    bool visible = false;
    uint64_t uploadedHash = 0;
    uint64_t lastRasterHash = 0;
    int uploadFailureCount = 0;
    bool uploadsDisabled = false;
    vr::EVROverlayError lastError = vr::VROverlayError_None;
    size_t lastVertexCount = 0;
    const char* lastSource = "none";
    BoundaryPreviewPlane lastPlane;
};

PreviewState& State()
{
    static PreviewState s;
    return s;
}

constexpr const char* kPreviewKey = "wkopenvr.boundary.preview";
constexpr const char* kPreviewName = "Boundary drawing preview";

const char* OverlayErrorName(vr::EVROverlayError err)
{
    switch (err) {
    case vr::VROverlayError_None: return "None";
    case vr::VROverlayError_UnknownOverlay: return "UnknownOverlay";
    case vr::VROverlayError_InvalidHandle: return "InvalidHandle";
    case vr::VROverlayError_PermissionDenied: return "PermissionDenied";
    case vr::VROverlayError_OverlayLimitExceeded: return "OverlayLimitExceeded";
    case vr::VROverlayError_WrongVisibilityType: return "WrongVisibilityType";
    case vr::VROverlayError_KeyTooLong: return "KeyTooLong";
    case vr::VROverlayError_NameTooLong: return "NameTooLong";
    case vr::VROverlayError_KeyInUse: return "KeyInUse";
    case vr::VROverlayError_WrongTransformType: return "WrongTransformType";
    case vr::VROverlayError_InvalidTrackedDevice: return "InvalidTrackedDevice";
    case vr::VROverlayError_InvalidParameter: return "InvalidParameter";
    case vr::VROverlayError_ThumbnailCantBeDestroyed: return "ThumbnailCantBeDestroyed";
    case vr::VROverlayError_ArrayTooSmall: return "ArrayTooSmall";
    case vr::VROverlayError_RequestFailed: return "RequestFailed";
    case vr::VROverlayError_InvalidTexture: return "InvalidTexture";
    case vr::VROverlayError_UnableToLoadFile: return "UnableToLoadFile";
    case vr::VROverlayError_KeyboardAlreadyInUse: return "KeyboardAlreadyInUse";
    case vr::VROverlayError_NoNeighbor: return "NoNeighbor";
    case vr::VROverlayError_TooManyMaskPrimitives: return "TooManyMaskPrimitives";
    case vr::VROverlayError_BadMaskPrimitive: return "BadMaskPrimitive";
    default: return "Unknown";
    }
}

uint64_t HashU64(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= kFnvPrime;
    return hash;
}

uint64_t HashVertex(uint64_t hash, const BoundaryVertex& v)
{
    hash = HashU64(hash, static_cast<uint64_t>(std::llround(v.x * 100.0)));
    hash = HashU64(hash, static_cast<uint64_t>(std::llround(v.y * 100.0)));
    hash = HashU64(hash, static_cast<uint64_t>(std::llround(v.z * 100.0)));
    return hash;
}

uint64_t HashRenderCommand(uint64_t hash, const SpatialRenderCommand& command)
{
    hash = HashU64(hash, static_cast<uint64_t>(command.kind));
    hash = HashU64(hash, command.closeLoop ? 1u : 0u);
    hash = HashU64(hash, static_cast<uint64_t>(command.layer));
    hash = HashU64(hash, static_cast<uint64_t>(command.style.r));
    hash = HashU64(hash, static_cast<uint64_t>(command.style.g));
    hash = HashU64(hash, static_cast<uint64_t>(command.style.b));
    hash = HashU64(hash, static_cast<uint64_t>(command.style.a));
    hash = HashU64(hash, static_cast<uint64_t>(command.style.fillA));
    hash = HashU64(hash, command.style.fill ? 1u : 0u);
    hash = HashU64(hash, static_cast<uint64_t>(std::llround(command.style.strokeMeters * 1000.0)));
    hash = HashU64(hash, static_cast<uint64_t>(std::llround(command.style.dotMeters * 1000.0)));
    hash = HashU64(hash, command.standingVertices.size());
    for (const auto& v : command.standingVertices) {
        hash = HashVertex(hash, v);
    }
    return hash;
}

int ClampPixel(double value)
{
    const int rounded = static_cast<int>(std::lround(value));
    return std::clamp(rounded, 0, BoundaryPreviewRaster::kTextureSize - 1);
}

uint8_t CoveredAlpha(uint8_t alpha, double coverage)
{
    const double scaled = static_cast<double>(alpha) * std::clamp(coverage, 0.0, 1.0);
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(scaled)), 0, 255));
}

void BlendPixel(std::vector<uint8_t>& pixels, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    constexpr int size = BoundaryPreviewRaster::kTextureSize;
    if (x < 0 || y < 0 || x >= size || y >= size) return;

    const size_t idx = static_cast<size_t>(y * size + x) * 4u;
    const uint16_t inv = static_cast<uint16_t>(255u - a);
    pixels[idx + 0] = static_cast<uint8_t>((static_cast<uint16_t>(pixels[idx + 0]) * inv + r * a) / 255u);
    pixels[idx + 1] = static_cast<uint8_t>((static_cast<uint16_t>(pixels[idx + 1]) * inv + g * a) / 255u);
    pixels[idx + 2] = static_cast<uint8_t>((static_cast<uint16_t>(pixels[idx + 2]) * inv + b * a) / 255u);
    pixels[idx + 3] = static_cast<uint8_t>(std::min<int>(255, pixels[idx + 3] + a));
}

double DistanceToSegment(double px, double py, double ax, double ay, double bx, double by)
{
    const double vx = bx - ax;
    const double vy = by - ay;
    const double lenSq = vx * vx + vy * vy;
    if (lenSq <= 1e-9) {
        const double dx = px - ax;
        const double dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    const double t = std::clamp(((px - ax) * vx + (py - ay) * vy) / lenSq, 0.0, 1.0);
    const double cx = ax + vx * t;
    const double cy = ay + vy * t;
    const double dx = px - cx;
    const double dy = py - cy;
    return std::sqrt(dx * dx + dy * dy);
}

void DrawSoftDot(
    std::vector<uint8_t>& pixels,
    int cx,
    int cy,
    double radius,
    double feather,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    const int reach = static_cast<int>(std::ceil(radius + feather));
    for (int y = cy - reach; y <= cy + reach; ++y) {
        for (int x = cx - reach; x <= cx + reach; ++x) {
            const double dx = (static_cast<double>(x) + 0.5) - static_cast<double>(cx);
            const double dy = (static_cast<double>(y) + 0.5) - static_cast<double>(cy);
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double coverage = (radius + feather - dist) / std::max(feather, 0.001);
            const uint8_t aa = CoveredAlpha(a, coverage);
            if (aa != 0) BlendPixel(pixels, x, y, r, g, b, aa);
        }
    }
}

void DrawSoftRing(
    std::vector<uint8_t>& pixels,
    int cx,
    int cy,
    double radius,
    double thickness,
    double feather,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    const double half = thickness * 0.5;
    const int reach = static_cast<int>(std::ceil(radius + half + feather));
    for (int y = cy - reach; y <= cy + reach; ++y) {
        for (int x = cx - reach; x <= cx + reach; ++x) {
            const double dx = (static_cast<double>(x) + 0.5) - static_cast<double>(cx);
            const double dy = (static_cast<double>(y) + 0.5) - static_cast<double>(cy);
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double edge = std::fabs(dist - radius);
            const double coverage = (half + feather - edge) / std::max(feather, 0.001);
            const uint8_t aa = CoveredAlpha(a, coverage);
            if (aa != 0) BlendPixel(pixels, x, y, r, g, b, aa);
        }
    }
}

void DrawSoftLine(
    std::vector<uint8_t>& pixels,
    int x0,
    int y0,
    int x1,
    int y1,
    double radius,
    double feather,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    const int reach = static_cast<int>(std::ceil(radius + feather));
    const int minX = std::min(x0, x1) - reach;
    const int maxX = std::max(x0, x1) + reach;
    const int minY = std::min(y0, y1) - reach;
    const int maxY = std::max(y0, y1) + reach;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double dist = DistanceToSegment(
                static_cast<double>(x) + 0.5,
                static_cast<double>(y) + 0.5,
                static_cast<double>(x0),
                static_cast<double>(y0),
                static_cast<double>(x1),
                static_cast<double>(y1));
            const double coverage = (radius + feather - dist) / std::max(feather, 0.001);
            const uint8_t aa = CoveredAlpha(a, coverage);
            if (aa != 0) BlendPixel(pixels, x, y, r, g, b, aa);
        }
    }
}

void FillPolygon(
    std::vector<uint8_t>& pixels,
    const std::vector<std::pair<int, int>>& points,
    const SpatialStyle& style)
{
    if (points.size() < 3) return;
    if (!style.fill || style.fillA == 0) return;

    constexpr int size = BoundaryPreviewRaster::kTextureSize;
    std::vector<double> intersections;
    intersections.reserve(points.size());

    for (int y = 0; y < size; ++y) {
        intersections.clear();
        const double scanY = static_cast<double>(y) + 0.5;
        for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            const auto& a = points[j];
            const auto& b = points[i];
            const double y0 = static_cast<double>(a.second);
            const double y1 = static_cast<double>(b.second);
            if ((y0 <= scanY && y1 > scanY) || (y1 <= scanY && y0 > scanY)) {
                const double x0 = static_cast<double>(a.first);
                const double x1 = static_cast<double>(b.first);
                const double t = (scanY - y0) / (y1 - y0);
                intersections.push_back(x0 + (x1 - x0) * t);
            }
        }

        std::sort(intersections.begin(), intersections.end());
        for (size_t i = 1; i < intersections.size(); i += 2) {
            const int xStart = std::clamp(
                static_cast<int>(std::ceil(intersections[i - 1])),
                0,
                size - 1);
            const int xEnd = std::clamp(
                static_cast<int>(std::floor(intersections[i])),
                0,
                size - 1);
            for (int x = xStart; x <= xEnd; ++x) {
                BlendPixel(pixels, x, y, style.r, style.g, style.b, style.fillA);
            }
        }
    }
}

void DrawBoundarySegment(
    std::vector<uint8_t>& pixels,
    const std::pair<int, int>& a,
    const std::pair<int, int>& b,
    bool closing,
    double strokeRadius,
    double strokeFeather,
    const SpatialStyle& style)
{
    if (style.strokeMeters <= 0.0 || style.a == 0) return;
    const uint8_t haloAlpha = static_cast<uint8_t>(std::clamp(
        static_cast<int>(style.a) / 4,
        0,
        255));
    const uint8_t midAlpha = static_cast<uint8_t>(std::clamp(
        static_cast<int>(style.a),
        0,
        255));
    DrawSoftLine(pixels, a.first, a.second, b.first, b.second,
        closing ? strokeRadius * 0.9 : strokeRadius,
        strokeFeather,
        style.r, style.g, style.b, haloAlpha);
    DrawSoftLine(pixels, a.first, a.second, b.first, b.second,
        closing ? strokeRadius * 0.35 : strokeRadius * 0.42,
        std::max(1.5, strokeFeather * 0.35),
        style.r, style.g, style.b, midAlpha);
    DrawSoftLine(pixels, a.first, a.second, b.first, b.second,
        std::max(1.25, strokeRadius * 0.14),
        1.5, 255, 255, 255, static_cast<uint8_t>(std::min<int>(style.a, 92)));
}

void DrawBoundaryVertex(
    std::vector<uint8_t>& pixels,
    const std::pair<int, int>& p,
    bool first,
    bool last,
    double dotRadius,
    const SpatialStyle& style)
{
    if (style.dotMeters <= 0.0 || style.a == 0) return;
    const uint8_t haloAlpha = style.fillA != 0
        ? style.fillA
        : static_cast<uint8_t>(std::min<int>(style.a / 4, 72));
    if (last || first) {
        DrawSoftDot(pixels, p.first, p.second, dotRadius * 1.45, dotRadius * 0.55,
            style.r, style.g, style.b, haloAlpha);
        DrawSoftRing(pixels, p.first, p.second, dotRadius,
            std::max(2.0, dotRadius * 0.25),
            2.0,
            style.r,
            style.g,
            style.b,
            static_cast<uint8_t>(std::min<int>(style.a, 240)));
        DrawSoftDot(pixels, p.first, p.second, dotRadius * 0.45, 2.0,
            style.r, style.g, style.b, static_cast<uint8_t>(std::min<int>(style.a, 255)));
        return;
    }

    DrawSoftDot(pixels, p.first, p.second, dotRadius * 0.70, dotRadius * 0.30,
        style.r, style.g, style.b, haloAlpha);
    DrawSoftDot(pixels, p.first, p.second, dotRadius * 0.36, 1.75,
        style.r, style.g, style.b, style.a);
}

bool EnsureCreated()
{
    auto& s = State();
    if (s.created) return true;
    if (!vr::VROverlay()) return false;

    vr::EVROverlayError err =
        vr::VROverlay()->CreateOverlay(kPreviewKey, kPreviewName, &s.handle);
    if (err == vr::VROverlayError_KeyInUse) {
        err = vr::VROverlay()->FindOverlay(kPreviewKey, &s.handle);
    }
    if (err != vr::VROverlayError_None) {
        std::fprintf(stderr, "[boundary-preview] CreateOverlay failed: %d\n", static_cast<int>(err));
        openvr_pair::common::DiagnosticLog("boundary-preview", "create_failed err=%d", static_cast<int>(err));
        return false;
    }

    vr::VROverlay()->SetOverlaySortOrder(s.handle, 12);
    vr::VROverlay()->SetOverlayAlpha(s.handle, 0.95f);
    s.created = true;
    s.visible = false;
    s.uploadedHash = 0;
    s.uploadsDisabled = false;
    s.lastError = vr::VROverlayError_None;
    openvr_pair::common::DiagnosticLog("boundary-preview", "created");
    openvr_pair::common::DiagnosticLog(
        "boundary_preview_status",
        "created=1 visible=0 uploads_disabled=0 failures=0 error=0 error_name=None source=create");
    return true;
}

void HideOnly(const char* source)
{
    auto& s = State();
    if (!s.created) return;
    if (vr::VROverlay() && s.handle != vr::k_ulOverlayHandleInvalid) {
        vr::VROverlay()->HideOverlay(s.handle);
    }
    if (s.visible) {
        openvr_pair::common::DiagnosticLog(
            "boundary_preview_status",
            "created=1 visible=0 uploads_disabled=%d failures=%d error=%d error_name=%s source=%s",
            s.uploadsDisabled ? 1 : 0,
            s.uploadFailureCount,
            static_cast<int>(s.lastError),
            OverlayErrorName(s.lastError),
            source ? source : s.lastSource);
    }
    s.visible = false;
}

void Destroy()
{
    auto& s = State();
    if (!s.created) return;

    if (vr::VROverlay() && s.handle != vr::k_ulOverlayHandleInvalid) {
        vr::VROverlay()->DestroyOverlay(s.handle);
    }
    s.handle = vr::k_ulOverlayHandleInvalid;
    s.created = false;
    s.visible = false;
    s.uploadedHash = 0;
    s.lastRasterHash = 0;
    s.uploadFailureCount = 0;
    s.uploadsDisabled = false;
    s.lastError = vr::VROverlayError_None;
    s.lastVertexCount = 0;
    openvr_pair::common::DiagnosticLog("boundary-preview", "destroyed");
}

double WorldMetersToPixels(double spanMeters, double meters, double minPixels, double maxPixels)
{
    const double pixels = meters / std::max(spanMeters, 0.001) *
        static_cast<double>(BoundaryPreviewRaster::kTextureSize - 1);
    return std::clamp(pixels, minPixels, maxPixels);
}

} // namespace

BoundaryPreviewPlane ComputeBoundaryPreviewPlane(
    const std::vector<BoundaryVertex>& vertices)
{
    BoundaryPreviewPlane out;
    if (vertices.empty()) return out;

    const PolygonBounds bounds = ComputePolygonBoundsXZ(vertices);
    const double rangeX = bounds.xMax - bounds.xMin;
    const double rangeZ = bounds.zMax - bounds.zMin;
    const double range = std::max(rangeX, rangeZ);

    out.valid = true;
    out.centerX = (bounds.xMin + bounds.xMax) * 0.5;
    out.centerZ = (bounds.zMin + bounds.zMax) * 0.5;
    out.spanMeters = std::max(kMinPreviewSpanMeters, range + kPreviewPadMeters * 2.0);
    return out;
}

BoundaryPreviewPlane ComputeBoundaryPreviewPlane(
    const std::vector<SpatialRenderCommand>& commands)
{
    std::vector<BoundaryVertex> allVertices;
    for (const SpatialRenderCommand& command : commands) {
        allVertices.insert(
            allVertices.end(),
            command.standingVertices.begin(),
            command.standingVertices.end());
    }
    return ComputeBoundaryPreviewPlane(allVertices);
}

BoundaryPreviewRaster BuildBoundaryPreviewRaster(
    const std::vector<BoundaryVertex>& vertices,
    bool closeLoop)
{
    SpatialSession session = BoundaryCaptureSessionDescriptor(
        StandingSpace(),
        -1,
        {},
        vertices.empty() ? 0.0 : vertices.front().y,
        false,
        0);
    SpatialPrimitive primitive = BoundaryPathPrimitive(session, vertices, closeLoop);
    return BuildBoundaryPreviewRaster(BuildSpatialRenderCommands({ primitive }));
}

BoundaryPreviewRaster BuildBoundaryPreviewRaster(
    const std::vector<SpatialRenderCommand>& commands)
{
    BoundaryPreviewRaster raster;
    raster.plane = ComputeBoundaryPreviewPlane(commands);
    raster.rgba.assign(
        static_cast<size_t>(BoundaryPreviewRaster::kTextureSize) *
            static_cast<size_t>(BoundaryPreviewRaster::kTextureSize) * 4u,
        0u);

    uint64_t hash = HashU64(kFnvOffset, commands.size());
    for (const SpatialRenderCommand& command : commands) {
        hash = HashRenderCommand(hash, command);
    }
    raster.hash = hash;

    if (!raster.plane.valid) return raster;

    const double half = raster.plane.spanMeters * 0.5;
    const double minX = raster.plane.centerX - half;
    const double maxZ = raster.plane.centerZ + half;
    const double scale = static_cast<double>(BoundaryPreviewRaster::kTextureSize - 1) /
        raster.plane.spanMeters;
    auto toPixel = [&](const BoundaryVertex& v) {
        const int x = ClampPixel((v.x - minX) * scale);
        const int y = ClampPixel((maxZ - v.z) * scale);
        return std::pair<int, int>(x, y);
    };

    for (const SpatialRenderCommand& command : commands) {
        const auto& vertices = command.standingVertices;
        if (vertices.empty()) {
            continue;
        }

        std::vector<std::pair<int, int>> pixelPoints;
        pixelPoints.reserve(vertices.size());
        for (const auto& v : vertices) {
            pixelPoints.push_back(toPixel(v));
        }

        if (command.closeLoop && command.style.fill && vertices.size() >= 3) {
            FillPolygon(raster.rgba, pixelPoints, command.style);
        }

        const double strokeRadius = command.style.strokeMeters > 0.0
            ? WorldMetersToPixels(
                raster.plane.spanMeters,
                command.style.strokeMeters,
                2.0,
                18.0)
            : 0.0;
        const double strokeFeather = command.style.strokeMeters > 0.0
            ? WorldMetersToPixels(
                raster.plane.spanMeters,
                command.style.strokeMeters * 0.40,
                1.5,
                8.0)
            : 0.0;
        const double dotRadius = command.style.dotMeters > 0.0
            ? WorldMetersToPixels(
                raster.plane.spanMeters,
                command.style.dotMeters,
                4.0,
                22.0)
            : 0.0;

        for (size_t i = 1; i < vertices.size(); ++i) {
            DrawBoundarySegment(
                raster.rgba,
                pixelPoints[i - 1],
                pixelPoints[i],
                false,
                strokeRadius,
                strokeFeather,
                command.style);
        }
        if (command.closeLoop && vertices.size() >= 3) {
            DrawBoundarySegment(
                raster.rgba,
                pixelPoints.back(),
                pixelPoints.front(),
                true,
                strokeRadius,
                strokeFeather,
                command.style);
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            DrawBoundaryVertex(
                raster.rgba,
                pixelPoints[i],
                i == 0,
                i + 1 == vertices.size(),
                dotRadius,
                command.style);
        }
    }
    return raster;
}

int BoundaryPreviewUploadFailureDisableThreshold()
{
    return kUploadFailureDisableThreshold;
}

bool BoundaryPreviewShouldDisableUploadsAfterFailureCount(int failureCount)
{
    return failureCount >= kUploadFailureDisableThreshold;
}

BoundaryPreviewStatus GetBoundaryPreviewStatus()
{
    const auto& s = State();
    BoundaryPreviewStatus status;
    status.created = s.created;
    status.visible = s.visible;
    status.uploadsDisabled = s.uploadsDisabled;
    status.uploadFailureCount = s.uploadFailureCount;
    status.lastError = static_cast<int>(s.lastError);
    status.lastErrorName = OverlayErrorName(s.lastError);
    status.uploadedHash = s.uploadedHash;
    status.lastRasterHash = s.lastRasterHash;
    status.lastVertexCount = s.lastVertexCount;
    status.lastSource = s.lastSource ? s.lastSource : "none";
    status.plane = s.lastPlane;
    return status;
}

void ResetBoundaryPreviewUploadFailures()
{
    auto& s = State();
    const bool wasDisabled = s.uploadsDisabled || s.uploadFailureCount > 0;
    s.uploadFailureCount = 0;
    s.uploadsDisabled = false;
    s.lastError = vr::VROverlayError_None;
    s.uploadedHash = 0;
    if (wasDisabled) {
        openvr_pair::common::DiagnosticLog(
            "boundary_preview_status",
            "created=%d visible=%d uploads_disabled=0 failures=0 error=0 error_name=None source=reset",
            s.created ? 1 : 0,
            s.visible ? 1 : 0);
    }
}

vr::ETrackingUniverseOrigin BoundaryPreviewTrackingOrigin()
{
    return vr::TrackingUniverseStanding;
}

vr::HmdMatrix34_t BoundaryPreviewTransform(
    double centerX,
    double floorY,
    double centerZ)
{
    vr::HmdMatrix34_t mat{};
    // Rotate the overlay onto the floor: local X -> world X, local Y -> -world Z,
    // local +Z normal -> world up.
    mat.m[0][0] = 1.0f;
    mat.m[0][1] = 0.0f;
    mat.m[0][2] = 0.0f;
    mat.m[0][3] = static_cast<float>(centerX);

    mat.m[1][0] = 0.0f;
    mat.m[1][1] = 0.0f;
    mat.m[1][2] = 1.0f;
    mat.m[1][3] = static_cast<float>(floorY + 0.025);

    mat.m[2][0] = 0.0f;
    mat.m[2][1] = -1.0f;
    mat.m[2][2] = 0.0f;
    mat.m[2][3] = static_cast<float>(centerZ);
    return mat;
}

void TickBoundaryPreview(
    bool wantVisible,
    const std::vector<BoundaryVertex>& vertices,
    double floorY,
    bool closeLoop,
    const char* source)
{
    SpatialSession session = BoundaryCaptureSessionDescriptor(
        StandingSpace(),
        -1,
        {},
        floorY,
        false,
        0);
    SpatialPrimitive primitive = BoundaryPathPrimitive(session, vertices, closeLoop);
    TickBoundaryPreview(
        wantVisible,
        BuildSpatialRenderCommands({ primitive }),
        floorY,
        source);
}

void TickBoundaryPreview(
    bool wantVisible,
    const std::vector<SpatialRenderCommand>& commands,
    double floorY,
    const char* source)
{
    auto& s = State();
    s.lastSource = source ? source : "unknown";
    size_t vertexCount = 0;
    for (const auto& command : commands) {
        vertexCount += command.standingVertices.size();
    }
    s.lastVertexCount = vertexCount;

    if (!wantVisible || commands.empty()) {
        HideOnly(source ? source : "hidden");
        return;
    }

    BoundaryPreviewRaster raster = BuildBoundaryPreviewRaster(commands);
    s.lastRasterHash = raster.hash;
    s.lastPlane = raster.plane;
    if (!raster.plane.valid) {
        HideOnly(source ? source : "invalid_plane");
        return;
    }
    if (!EnsureCreated()) return;

    if (s.uploadsDisabled) {
        return;
    }
    if (raster.hash != s.uploadedHash) {
        vr::EVROverlayError err = vr::VROverlay()->SetOverlayRaw(
            s.handle,
            raster.rgba.data(),
            BoundaryPreviewRaster::kTextureSize,
            BoundaryPreviewRaster::kTextureSize,
            4);
        if (err == vr::VROverlayError_None) {
            s.uploadedHash = raster.hash;
            s.uploadFailureCount = 0;
            s.lastError = vr::VROverlayError_None;
        } else {
            s.lastError = err;
            ++s.uploadFailureCount;
            if (s.uploadFailureCount <= 3 || (s.uploadFailureCount % 30) == 0) {
                openvr_pair::common::DiagnosticLog(
                    "boundary-preview",
                    "upload_failed err=%d error_name=%s count=%d source=%s vertices=%zu hash=%llu",
                    static_cast<int>(err),
                    OverlayErrorName(err),
                    s.uploadFailureCount,
                    s.lastSource,
                    s.lastVertexCount,
                    static_cast<unsigned long long>(raster.hash));
                openvr_pair::common::DiagnosticLog(
                    "boundary_preview_status",
                    "created=%d visible=%d uploads_disabled=%d failures=%d error=%d error_name=%s source=%s vertices=%zu hash=%llu",
                    s.created ? 1 : 0,
                    s.visible ? 1 : 0,
                    s.uploadsDisabled ? 1 : 0,
                    s.uploadFailureCount,
                    static_cast<int>(err),
                    OverlayErrorName(err),
                    s.lastSource,
                    s.lastVertexCount,
                    static_cast<unsigned long long>(raster.hash));
            }
            if (BoundaryPreviewShouldDisableUploadsAfterFailureCount(s.uploadFailureCount)) {
                if (vr::VROverlay() && s.handle != vr::k_ulOverlayHandleInvalid) {
                    vr::VROverlay()->HideOverlay(s.handle);
                }
                s.visible = false;
                s.uploadsDisabled = true;
                openvr_pair::common::DiagnosticLog(
                    "boundary-preview",
                    "uploads_disabled_after_failures err=%d error_name=%s source=%s vertices=%zu",
                    static_cast<int>(err),
                    OverlayErrorName(err),
                    s.lastSource,
                    s.lastVertexCount);
                openvr_pair::common::DiagnosticLog(
                    "boundary_preview_status",
                    "created=%d visible=0 uploads_disabled=1 failures=%d error=%d error_name=%s source=%s vertices=%zu",
                    s.created ? 1 : 0,
                    s.uploadFailureCount,
                    static_cast<int>(err),
                    OverlayErrorName(err),
                    s.lastSource,
                    s.lastVertexCount);
            }
            return;
        }
    }

    vr::VROverlay()->SetOverlayWidthInMeters(
        s.handle,
        static_cast<float>(raster.plane.spanMeters));
    vr::HmdMatrix34_t mat = BoundaryPreviewTransform(
        raster.plane.centerX,
        floorY,
        raster.plane.centerZ);
    vr::VROverlay()->SetOverlayTransformAbsolute(
        s.handle,
        BoundaryPreviewTrackingOrigin(),
        &mat);
    vr::VROverlay()->ShowOverlay(s.handle);
    if (!s.visible) {
        openvr_pair::common::DiagnosticLog(
            "boundary_preview_status",
            "created=1 visible=1 uploads_disabled=0 failures=%d error=%d error_name=%s source=%s vertices=%zu span=%.3f center=(%.3f,%.3f)",
            s.uploadFailureCount,
            static_cast<int>(s.lastError),
            OverlayErrorName(s.lastError),
            s.lastSource,
            s.lastVertexCount,
            raster.plane.spanMeters,
            raster.plane.centerX,
            raster.plane.centerZ);
    }
    s.visible = true;
}

} // namespace wkopenvr::boundary
