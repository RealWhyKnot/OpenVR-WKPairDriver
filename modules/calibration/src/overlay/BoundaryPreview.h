#pragma once

#include "Calibration.h"
#include "BoundarySpatial.h"

#include <openvr.h>

#include <cstdint>
#include <vector>

namespace wkopenvr::boundary {

struct BoundaryPreviewPlane {
    bool valid = false;
    double centerX = 0.0;
    double centerZ = 0.0;
    double spanMeters = 0.0;
};

struct BoundaryPreviewRaster {
    static constexpr int kTextureSize = 512;

    BoundaryPreviewPlane plane;
    std::vector<uint8_t> rgba;
    uint64_t hash = 0;
};

BoundaryPreviewPlane ComputeBoundaryPreviewPlane(
    const std::vector<BoundaryVertex>& vertices);

BoundaryPreviewPlane ComputeBoundaryPreviewPlane(
    const std::vector<SpatialRenderCommand>& commands);

BoundaryPreviewRaster BuildBoundaryPreviewRaster(
    const std::vector<BoundaryVertex>& vertices,
    bool closeLoop);

BoundaryPreviewRaster BuildBoundaryPreviewRaster(
    const std::vector<SpatialRenderCommand>& commands);

int BoundaryPreviewUploadFailureDisableThreshold();

bool BoundaryPreviewShouldDisableUploadsAfterFailureCount(int failureCount);

vr::ETrackingUniverseOrigin BoundaryPreviewTrackingOrigin();

vr::HmdMatrix34_t BoundaryPreviewTransform(
    double centerX,
    double floorY,
    double centerZ);

void TickBoundaryPreview(
    bool wantVisible,
    const std::vector<BoundaryVertex>& vertices,
    double floorY,
    bool closeLoop);

void TickBoundaryPreview(
    bool wantVisible,
    const std::vector<SpatialRenderCommand>& commands,
    double floorY);

} // namespace wkopenvr::boundary
