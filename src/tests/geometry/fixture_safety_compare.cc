// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_safety_compare.hh"

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

  std::string ToString(const G4ThreeVector& vector) {
    std::ostringstream buffer;
    buffer << "(" << vector.x() << ", " << vector.y() << ", " << vector.z() << ")";
    return buffer.str();
  }

  std::string DistanceString(const G4double distance) {
    if (std::isinf(distance)) {
      return "kInfinity";
    }
    std::ostringstream buf;
    buf << distance;
    return buf.str();
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

  /**
   * Generate bounding-box sample points using a 3-D Halton sequence.
   *
   * @param solid  Solid whose BoundingLimits() defines the sampling volume.
   * @param count  Number of points to produce.
   * @return       Vector of 3-D points distributed across the bounding box.
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

} // namespace

ValidationReport CompareFixtureSafety(const FixtureValidationRequest& request,
                                      const FixtureSafetyComparisonOptions& options,
                                      FixtureSafetyComparisonSummary* summary) {
  ValidationReport report;
  if (options.point_count == 0U) {
    report.AddError("fixture.safety_compare_invalid_count", "Point count must be positive",
                    request.manifest.source_path);
    return report;
  }

  FixtureSafetyComparisonSummary local_summary;
  local_summary.fixture_id = request.manifest.family + "/" + request.fixture.id;

  try {
    const auto provenance_path = ResolveFixtureProvenancePath(request.manifest, request.fixture);
    const FixtureProvenance provenance = ParseFixtureProvenance(provenance_path);
    local_summary.geant4_class         = Geant4Class(provenance);

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolid(provenance);
    auto imported_solid =
        std::make_unique<G4OCCTSolid>(request.fixture.id + "_imported", LoadImportedShape(request));

    const G4double surface_tolerance = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();

    // ── Generate test points ──────────────────────────────────────────────
    const std::vector<G4ThreeVector> all_points =
        GenerateBoundingBoxPoints(*native_solid, options.point_count);

    // ── Classify each point using the native solid ────────────────────────
    std::vector<G4ThreeVector> outside_points; // → DistanceToIn(p)
    std::vector<G4ThreeVector> inside_points;  // → DistanceToOut(p)
    outside_points.reserve(all_points.size());
    inside_points.reserve(all_points.size());
    for (const auto& point : all_points) {
      const EInside state = native_solid->Inside(point);
      if (state == kOutside) {
        outside_points.push_back(point);
      } else if (state == kInside) {
        inside_points.push_back(point);
      }
      // kSurface points are skipped: safety distance is 0 by definition.
    }

    local_summary.point_count = outside_points.size() + inside_points.size();

    // ── Time DistanceToIn(p) on outside points ────────────────────────────
    std::vector<G4double> native_safety_in;
    native_safety_in.reserve(outside_points.size());
    const auto native_in_begin = std::chrono::steady_clock::now();
    for (const auto& point : outside_points) {
      native_safety_in.push_back(native_solid->DistanceToIn(point));
    }
    const auto native_in_end = std::chrono::steady_clock::now();
    local_summary.native_safety_in_ms =
        std::chrono::duration<double, std::milli>(native_in_end - native_in_begin).count();

    std::vector<G4double> imported_safety_in;
    imported_safety_in.reserve(outside_points.size());
    const auto imported_in_begin = std::chrono::steady_clock::now();
    for (const auto& point : outside_points) {
      imported_safety_in.push_back(imported_solid->DistanceToIn(point));
    }
    const auto imported_in_end = std::chrono::steady_clock::now();
    local_summary.imported_safety_in_ms =
        std::chrono::duration<double, std::milli>(imported_in_end - imported_in_begin).count();

    // ── Time DistanceToOut(p) on inside points ────────────────────────────
    std::vector<G4double> native_safety_out;
    native_safety_out.reserve(inside_points.size());
    const auto native_out_begin = std::chrono::steady_clock::now();
    for (const auto& point : inside_points) {
      native_safety_out.push_back(native_solid->DistanceToOut(point));
    }
    const auto native_out_end = std::chrono::steady_clock::now();
    local_summary.native_safety_out_ms =
        std::chrono::duration<double, std::milli>(native_out_end - native_out_begin).count();

    std::vector<G4double> imported_safety_out;
    imported_safety_out.reserve(inside_points.size());
    const auto imported_out_begin = std::chrono::steady_clock::now();
    for (const auto& point : inside_points) {
      imported_safety_out.push_back(imported_solid->DistanceToOut(point));
    }
    const auto imported_out_end = std::chrono::steady_clock::now();
    local_summary.imported_safety_out_ms =
        std::chrono::duration<double, std::milli>(imported_out_end - imported_out_begin).count();

    // ── Compare DistanceToIn(p) results ───────────────────────────────────
    std::size_t reported_in_mismatches = 0;
    for (std::size_t index = 0; index < outside_points.size(); ++index) {
      const G4double native_dist   = native_safety_in[index];
      const G4double imported_dist = imported_safety_in[index];
      const G4double max_dist      = std::max(native_dist, imported_dist);
      const G4double tolerance     = std::max(surface_tolerance, 0.01 * max_dist);
      const G4double delta         = std::fabs(native_dist - imported_dist);
      if (delta > tolerance) {
        ++local_summary.safety_in_mismatch_count;
        if (reported_in_mismatches < options.max_reported_mismatches) {
          ++reported_in_mismatches;
          std::ostringstream message;
          message << "Point " << index << " DistanceToIn(p) mismatch for fixture '"
                  << request.fixture.id << "': native=" << DistanceString(native_dist)
                  << ", imported=" << DistanceString(imported_dist) << ", |delta|=" << delta
                  << ", tolerance=" << tolerance << ", point=" << ToString(outside_points[index]);
          report.AddError("fixture.safety_in_distance_mismatch", message.str(), provenance_path);
        }
      }
    }

    // ── Compare DistanceToOut(p) results ──────────────────────────────────
    std::size_t reported_out_mismatches = 0;
    for (std::size_t index = 0; index < inside_points.size(); ++index) {
      const G4double native_dist   = native_safety_out[index];
      const G4double imported_dist = imported_safety_out[index];
      const G4double max_dist      = std::max(native_dist, imported_dist);
      const G4double tolerance     = std::max(surface_tolerance, 0.01 * max_dist);
      const G4double delta         = std::fabs(native_dist - imported_dist);
      if (delta > tolerance) {
        ++local_summary.safety_out_mismatch_count;
        if (reported_out_mismatches < options.max_reported_mismatches) {
          ++reported_out_mismatches;
          std::ostringstream message;
          message << "Point " << index << " DistanceToOut(p) mismatch for fixture '"
                  << request.fixture.id << "': native=" << DistanceString(native_dist)
                  << ", imported=" << DistanceString(imported_dist) << ", |delta|=" << delta
                  << ", tolerance=" << tolerance << ", point=" << ToString(inside_points[index]);
          report.AddError("fixture.safety_out_distance_mismatch", message.str(), provenance_path);
        }
      }
    }

    // ── Summary info message ──────────────────────────────────────────────
    std::ostringstream timing_summary;
    timing_summary << "Safety distances for fixture '" << request.fixture.id << "' ("
                   << local_summary.geant4_class << "): points=" << local_summary.point_count
                   << " (outside=" << outside_points.size() << ", inside=" << inside_points.size()
                   << "); DistanceToIn: native=" << local_summary.native_safety_in_ms
                   << " ms, imported=" << local_summary.imported_safety_in_ms
                   << " ms, mismatches=" << local_summary.safety_in_mismatch_count
                   << "; DistanceToOut: native=" << local_summary.native_safety_out_ms
                   << " ms, imported=" << local_summary.imported_safety_out_ms
                   << " ms, mismatches=" << local_summary.safety_out_mismatch_count;
    report.AddInfo("fixture.safety_compare_summary", timing_summary.str(), provenance_path);

  } catch (const std::exception& error) {
    report.AddError("fixture.safety_compare_failed",
                    std::string("Fixture safety comparison failed: ") + error.what(),
                    ResolveFixtureProvenancePath(request.manifest, request.fixture));
  }

  if (summary != nullptr) {
    *summary = local_summary;
  }
  return report;
}

} // namespace g4occt::tests::geometry
