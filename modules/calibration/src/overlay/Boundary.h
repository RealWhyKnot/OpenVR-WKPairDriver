#pragma once

#include "Calibration.h"

#include <Eigen/Geometry>
#include <cstdint>
#include <vector>

namespace wkopenvr::boundary {

// Top-down projection: drop Y, keep X and Z.
struct XZPoint { double x, z; };

// Polygon area on the XZ plane (signed). Clockwise winding gives negative.
double SignedAreaXZ(const std::vector<XZPoint>& poly);
double AbsoluteAreaXZ(const std::vector<XZPoint>& poly);

// Project boundary vertices to the XZ plane.
std::vector<XZPoint> ProjectXZ(const std::vector<BoundaryVertex>& v);

// Douglas-Peucker simplification of a 3D path. Returns the kept indices in
// input order. epsilonMeters is the max perpendicular deviation permitted.
std::vector<size_t> SimplifyDouglasPeucker(const std::vector<BoundaryVertex>& path,
                                            double epsilonMeters);

// Apply the continuous-cal SE(3) transform to convert lighthouse-space
// boundary vertices into standing-universe coordinates.
std::vector<BoundaryVertex> TransformToStandingUniverse(
    const std::vector<BoundaryVertex>& lighthouseSpace,
    const Eigen::AffineCompact3d& lighthouseToStanding);

// Push the polygon to SteamVR chaperone. Returns false if VRChaperoneSetup
// is unavailable or any call fails. Builds vertical wall quads from floorY
// to ceilingY, pushes a bounding-rect play area size, and commits to Live.
bool PushToChaperone(const std::vector<BoundaryVertex>& standingUniverseVertices,
                     double floorY, double ceilingY);

// Capture the live chaperone collision bounds for later restore.
// Returns serialized bytes suitable for storage in BoundaryConfig::priorChaperone.
std::vector<uint8_t> SnapshotCurrentChaperone();

// Restore chaperone from bytes produced by SnapshotCurrentChaperone.
// Returns false if snapshot bytes don't parse.
bool RestoreChaperoneFromSnapshot(const std::vector<uint8_t>& snapshot);

// Axis-aligned bounding rectangle of the boundary polygon on the XZ plane.
struct PolygonBounds { double xMin, xMax, zMin, zMax; };
PolygonBounds ComputePolygonBoundsXZ(const std::vector<BoundaryVertex>& v);

}  // namespace wkopenvr::boundary
