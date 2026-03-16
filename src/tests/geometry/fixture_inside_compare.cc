// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_inside_compare.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_solid_builder.hh"
#include "geometry/fixture_validation.hh"

#include <G4GeometryTolerance.hh>
#include <G4ThreeVector.hh>

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace g4occt::tests::geometry {
namespace {

  // ──────────────────────────────────────────────────────────────────────────
  // String helpers
  // ──────────────────────────────────────────────────────────────────────────

  std::string ToString(const EInside state) {
    switch (state) {
    case kInside:
      return "kInside";
    case kSurface:
      return "kSurface";
    case kOutside:
      return "kOutside";
    }
    return "<unknown EInside>";
  }

  std::string ToString(const G4ThreeVector& vector) {
    std::ostringstream buffer;
    buffer << "(" << vector.x() << ", " << vector.y() << ", " << vector.z() << ")";
    return buffer.str();
  }

  // ──────────────────────────────────────────────────────────────────────────
  // Halton low-discrepancy sequence for deterministic bounding-box sampling
  // ──────────────────────────────────────────────────────────────────────────

  /// Prime bases for the 3-D Halton sequence; coprime bases give low discrepancy.
  constexpr std::size_t kHaltonBaseX = 2U;
  constexpr std::size_t kHaltonBaseY = 3U;
  constexpr std::size_t kHaltonBaseZ = 5U;

  /// Compute the i-th term of the Halton sequence in the given base.
  double Halton(std::size_t index, std::size_t base) {
    double result   = 0.0;
    double fraction = 1.0;
    while (index > 0U) {
      fraction /= static_cast<double>(base);
      result += fraction * static_cast<double>(index % base);
      index /= base;
    }
    return result;
  }

  // ──────────────────────────────────────────────────────────────────────────
  // Test-point generation
  // ──────────────────────────────────────────────────────────────────────────

  /**
   * Generate bounding-box sample points using a 3-D Halton sequence.
   *
   * @param solid      Solid whose BoundingLimits() defines the sampling volume.
   * @param count      Number of points to produce.
   * @return           Vector of 3-D points distributed across the bounding box.
   */
  std::vector<G4ThreeVector> GenerateBoundingBoxPoints(const G4VSolid& solid,
                                                       const std::size_t count) {
    G4ThreeVector bb_min;
    G4ThreeVector bb_max;
    solid.BoundingLimits(bb_min, bb_max);
    const G4ThreeVector extents = bb_max - bb_min;

    std::vector<G4ThreeVector> points;
    points.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const std::size_t i = index + 1U; // Halton is 1-indexed conventionally
      const double x      = bb_min.x() + Halton(i, kHaltonBaseX) * extents.x();
      const double y      = bb_min.y() + Halton(i, kHaltonBaseY) * extents.y();
      const double z      = bb_min.z() + Halton(i, kHaltonBaseZ) * extents.z();
      points.emplace_back(x, y, z);
    }
    return points;
  }

  /**
   * Generate near-surface test points by tracing rays from the bounding-box
   * centre and offsetting each hit inward and outward by one surface tolerance.
   *
   * For each ray direction:
   *  1. Trace DistanceToIn / DistanceToOut from the centre.
   *  2. If the ray intersects, create two points at `hit ∓ tolerance * direction`
   *     (one inside, one outside).
   *
   * At most `max_points` points are appended to `out`; the function stops as
   * soon as that limit is reached.
   *
   * @param solid      Solid to trace rays against.
   * @param tolerance  Offset magnitude (typically kCarTolerance / kSurfaceTolerance).
   * @param ray_count  Number of ray directions to use.
   * @param max_points Maximum number of points to append to `out`.
   * @param out        Destination vector (points are appended).
   */
  void GenerateNearSurfacePoints(const G4VSolid& solid, const G4double tolerance,
                                 const std::size_t ray_count, const std::size_t max_points,
                                 std::vector<G4ThreeVector>& out) {
    if (max_points == 0U) {
      return;
    }

    const std::size_t start_size = out.size();
    const G4ThreeVector center   = BoundingBoxCenter(solid);
    const EInside center_state   = solid.Inside(center);

    const std::vector<G4ThreeVector> directions = GenerateDirections(ray_count);

    for (const auto& direction : directions) {
      if (out.size() - start_size >= max_points) {
        break;
      }

      G4double distance = kInfinity;
      if (center_state == kOutside) {
        distance = solid.DistanceToIn(center, direction);
      } else {
        distance = solid.DistanceToOut(center, direction);
      }

      if (std::isinf(distance)) {
        continue;
      }

      const G4ThreeVector hit = center + distance * direction;

      // Point offset inward (toward centre) — subtract one tolerance step.
      if (out.size() - start_size < max_points) {
        out.push_back(hit - tolerance * direction);
      }
      // Point offset outward (away from centre) — add one tolerance step.
      if (out.size() - start_size < max_points) {
        out.push_back(hit + tolerance * direction);
      }
    }
  }

} // namespace

ValidationReport CompareFixtureInside(const FixtureValidationRequest& request,
                                      const FixtureInsideComparisonOptions& options,
                                      FixtureInsideComparisonSummary* summary) {
  ValidationReport report;
  if (options.point_count == 0U) {
    report.AddError("fixture.inside_compare_invalid_count", "Point count must be positive",
                    request.manifest.source_path);
    return report;
  }

  FixtureInsideComparisonSummary local_summary;
  local_summary.fixture_id = request.manifest.family + "/" + request.fixture.id;

  try {
    const auto provenance_path = ResolveFixtureProvenancePath(request.manifest, request.fixture);
    const FixtureProvenance provenance = ParseFixtureProvenance(provenance_path);
    local_summary.geant4_class         = Geant4Class(provenance);

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolid(provenance);
    auto imported_solid =
        std::make_unique<G4OCCTSolid>(request.fixture.id + "_imported", LoadImportedShape(request));

    const G4double tolerance = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();

    // ── Generate test points ──────────────────────────────────────────────
    // ~50% bounding-box points, ~50% near-surface points.
    const std::size_t bb_count      = options.point_count / 2U;
    const std::size_t surface_count = options.point_count - bb_count;
    // Each ray produces at most 2 near-surface points.
    const std::size_t surface_ray_count = surface_count;

    std::vector<G4ThreeVector> test_points = GenerateBoundingBoxPoints(*native_solid, bb_count);
    GenerateNearSurfacePoints(*native_solid, tolerance, surface_ray_count, surface_count,
                              test_points);

    local_summary.point_count = test_points.size();

    // ── Time native Inside() ──────────────────────────────────────────────
    std::vector<EInside> native_results;
    native_results.reserve(test_points.size());
    const auto native_begin = std::chrono::steady_clock::now();
    for (const auto& point : test_points) {
      native_results.push_back(native_solid->Inside(point));
    }
    const auto native_end = std::chrono::steady_clock::now();
    local_summary.native_elapsed_ms =
        std::chrono::duration<double, std::milli>(native_end - native_begin).count();

    // ── Time imported Inside() ────────────────────────────────────────────
    std::vector<EInside> imported_results;
    imported_results.reserve(test_points.size());
    const auto imported_begin = std::chrono::steady_clock::now();
    for (const auto& point : test_points) {
      imported_results.push_back(imported_solid->Inside(point));
    }
    const auto imported_end = std::chrono::steady_clock::now();
    local_summary.imported_elapsed_ms =
        std::chrono::duration<double, std::milli>(imported_end - imported_begin).count();

    // ── Compare results ───────────────────────────────────────────────────
    std::size_t reported_hard_mismatches = 0;
    std::size_t reported_ambiguities     = 0;
    for (std::size_t index = 0; index < test_points.size(); ++index) {
      const EInside native_state   = native_results[index];
      const EInside imported_state = imported_results[index];

      if (native_state == imported_state) {
        continue;
      }

      // kSurface vs kInside / kOutside: ambiguous boundary → warning only.
      if (native_state == kSurface || imported_state == kSurface) {
        ++local_summary.surface_ambiguity_count;
        if (reported_ambiguities < options.max_reported_mismatches) {
          ++reported_ambiguities;
          std::ostringstream message;
          message << "Point " << index << " surface-boundary ambiguity for fixture '"
                  << request.fixture.id << "': native=" << ToString(native_state)
                  << ", imported=" << ToString(imported_state)
                  << ", point=" << ToString(test_points[index]);
          report.AddWarning("fixture.inside_surface_ambiguity", message.str(), provenance_path);
        }
        continue;
      }

      // kInside vs kOutside: hard disagreement → error.
      ++local_summary.mismatch_count;
      if (reported_hard_mismatches < options.max_reported_mismatches) {
        ++reported_hard_mismatches;
        std::ostringstream message;
        message << "Point " << index << " classification mismatch for fixture '"
                << request.fixture.id << "': native=" << ToString(native_state)
                << ", imported=" << ToString(imported_state)
                << ", point=" << ToString(test_points[index]);
        report.AddError("fixture.inside_classification_mismatch", message.str(), provenance_path);
      }
    }

    // ── Summary info message ──────────────────────────────────────────────
    std::ostringstream timing_summary;
    timing_summary << "Classified " << local_summary.point_count << " points for fixture '"
                   << request.fixture.id << "' (" << local_summary.geant4_class
                   << "); native=" << local_summary.native_elapsed_ms
                   << " ms, imported=" << local_summary.imported_elapsed_ms
                   << " ms, hard_mismatches=" << local_summary.mismatch_count
                   << ", surface_ambiguities=" << local_summary.surface_ambiguity_count;
    report.AddInfo("fixture.inside_compare_summary", timing_summary.str(), provenance_path);

  } catch (const std::exception& error) {
    report.AddError("fixture.inside_compare_failed",
                    std::string("Fixture inside comparison failed: ") + error.what(),
                    ResolveFixtureProvenancePath(request.manifest, request.fixture));
  }

  if (summary != nullptr) {
    *summary = local_summary;
  }
  return report;
}

} // namespace g4occt::tests::geometry
