#pragma once

#include "Calibration.h"

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
    static constexpr int kTextureSize = 256;

    BoundaryPreviewPlane plane;
    std::vector<uint8_t> rgba;
    uint64_t hash = 0;
};

BoundaryPreviewPlane ComputeBoundaryPreviewPlane(
    const std::vector<BoundaryVertex>& vertices);

BoundaryPreviewRaster BuildBoundaryPreviewRaster(
    const std::vector<BoundaryVertex>& vertices,
    bool closeLoop);

void TickBoundaryPreview(
    bool wantVisible,
    const std::vector<BoundaryVertex>& vertices,
    double floorY,
    bool closeLoop);

} // namespace wkopenvr::boundary
