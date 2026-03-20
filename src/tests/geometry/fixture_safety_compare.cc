// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "geometry/fixture_safety_compare.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_solid_builder.hh"
#include "geometry/fixture_validation.hh"

#include <G4GeometryTolerance.hh>
#include <G4ThreeVector.hh>

#include <algorithm>
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

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolidForRequest(request, provenance);
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

    // ── Time ExactDistanceToIn(p) on outside points ───────────────────────
    std::vector<G4double> exact_safety_in;
    exact_safety_in.reserve(outside_points.size());
    const auto exact_in_begin = std::chrono::steady_clock::now();
    for (const auto& point : outside_points) {
      exact_safety_in.push_back(imported_solid->ExactDistanceToIn(point));
    }
    const auto exact_in_end = std::chrono::steady_clock::now();
    local_summary.exact_safety_in_ms =
        std::chrono::duration<double, std::milli>(exact_in_end - exact_in_begin).count();

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

    // ── Time ExactDistanceToOut(p) on inside points ───────────────────────
    std::vector<G4double> exact_safety_out;
    exact_safety_out.reserve(inside_points.size());
    const auto exact_out_begin = std::chrono::steady_clock::now();
    for (const auto& point : inside_points) {
      exact_safety_out.push_back(imported_solid->ExactDistanceToOut(point));
    }
    const auto exact_out_end = std::chrono::steady_clock::now();
    local_summary.exact_safety_out_ms =
        std::chrono::duration<double, std::milli>(exact_out_end - exact_out_begin).count();

    // ── Within OCCT: lower-bound violations and avg lb ratio ──────────────
    //
    // The accelerated DistanceToIn/Out(p) must be a lower bound on the exact
    // value by construction.  Any violation is a hard-fail error.
    // Also accumulate the ratio imported/exact for informational analysis.
    std::size_t reported_in_violations = 0;
    double dti_lb_ratio_sum            = 0.0;
    std::size_t dti_lb_ratio_count     = 0;
    // Always emit at least one error when violations exist; cap per-point detail messages.
    const std::size_t max_violations = std::max(std::size_t{1}, options.max_reported_violations);

    for (std::size_t index = 0; index < outside_points.size(); ++index) {
      const G4double lb_dist    = imported_safety_in[index];
      const G4double exact_dist = exact_safety_in[index];

      // Hard-fail if lower bound exceeds exact distance beyond tolerance.
      if (lb_dist > exact_dist + surface_tolerance) {
        ++local_summary.occt_lower_bound_in_violations;
        if (reported_in_violations < max_violations) {
          ++reported_in_violations;
          std::ostringstream message;
          message << "Point " << index
                  << " OCCT DistanceToIn(p) lower-bound violation for fixture '"
                  << request.fixture.id << "': lb=" << DistanceString(lb_dist)
                  << ", exact=" << DistanceString(exact_dist)
                  << ", point=" << ToString(outside_points[index]);
          report.AddError("fixture.occt_lower_bound_in_violation", message.str(), provenance_path);
        }
      }

      // Accumulate lower-bound ratio where both values are finite and exact > 0.
      if (std::isfinite(lb_dist) && std::isfinite(exact_dist) && exact_dist > surface_tolerance) {
        dti_lb_ratio_sum += lb_dist / exact_dist;
        ++dti_lb_ratio_count;
      }
    }
    local_summary.avg_dti_lb_ratio =
        (dti_lb_ratio_count > 0U) ? dti_lb_ratio_sum / static_cast<double>(dti_lb_ratio_count)
                                  : 0.0;

    std::size_t reported_out_violations = 0;
    double dto_lb_ratio_sum             = 0.0;
    std::size_t dto_lb_ratio_count      = 0;

    for (std::size_t index = 0; index < inside_points.size(); ++index) {
      const G4double lb_dist    = imported_safety_out[index];
      const G4double exact_dist = exact_safety_out[index];

      // Hard-fail if lower bound exceeds exact distance beyond tolerance.
      if (lb_dist > exact_dist + surface_tolerance) {
        ++local_summary.occt_lower_bound_out_violations;
        if (reported_out_violations < max_violations) {
          ++reported_out_violations;
          std::ostringstream message;
          message << "Point " << index
                  << " OCCT DistanceToOut(p) lower-bound violation for fixture '"
                  << request.fixture.id << "': lb=" << DistanceString(lb_dist)
                  << ", exact=" << DistanceString(exact_dist)
                  << ", point=" << ToString(inside_points[index]);
          report.AddError("fixture.occt_lower_bound_out_violation", message.str(), provenance_path);
        }
      }

      // Accumulate lower-bound ratio where both values are finite and exact > 0.
      if (std::isfinite(lb_dist) && std::isfinite(exact_dist) && exact_dist > surface_tolerance) {
        dto_lb_ratio_sum += lb_dist / exact_dist;
        ++dto_lb_ratio_count;
      }
    }
    local_summary.avg_dto_lb_ratio =
        (dto_lb_ratio_count > 0U) ? dto_lb_ratio_sum / static_cast<double>(dto_lb_ratio_count)
                                  : 0.0;

    // ── Between Geant4 and OCCT: average distance ratio ───────────────────
    //
    // For each point, accumulate the ratio imported/native so the caller can
    // determine whether OCCT gives systematically smaller or larger safeties.
    double dti_g4_occt_ratio_sum        = 0.0;
    std::size_t dti_g4_occt_ratio_count = 0;

    for (std::size_t index = 0; index < outside_points.size(); ++index) {
      const G4double native_dist   = native_safety_in[index];
      const G4double imported_dist = imported_safety_in[index];
      if (std::isfinite(native_dist) && native_dist > surface_tolerance &&
          std::isfinite(imported_dist)) {
        dti_g4_occt_ratio_sum += imported_dist / native_dist;
        ++dti_g4_occt_ratio_count;
      }
    }
    local_summary.avg_dti_g4_occt_ratio =
        (dti_g4_occt_ratio_count > 0U)
            ? dti_g4_occt_ratio_sum / static_cast<double>(dti_g4_occt_ratio_count)
            : 0.0;

    double dto_g4_occt_ratio_sum        = 0.0;
    std::size_t dto_g4_occt_ratio_count = 0;

    for (std::size_t index = 0; index < inside_points.size(); ++index) {
      const G4double native_dist   = native_safety_out[index];
      const G4double imported_dist = imported_safety_out[index];
      if (std::isfinite(native_dist) && native_dist > surface_tolerance &&
          std::isfinite(imported_dist)) {
        dto_g4_occt_ratio_sum += imported_dist / native_dist;
        ++dto_g4_occt_ratio_count;
      }
    }
    local_summary.avg_dto_g4_occt_ratio =
        (dto_g4_occt_ratio_count > 0U)
            ? dto_g4_occt_ratio_sum / static_cast<double>(dto_g4_occt_ratio_count)
            : 0.0;

    // ── Summary info message ──────────────────────────────────────────────
    std::ostringstream timing_summary;
    timing_summary << "Safety distances for fixture '" << request.fixture.id << "' ("
                   << local_summary.geant4_class << "): points=" << local_summary.point_count
                   << " (outside=" << outside_points.size() << ", inside=" << inside_points.size()
                   << "); DTI: g4=" << local_summary.native_safety_in_ms
                   << " ms, occt=" << local_summary.imported_safety_in_ms
                   << " ms, exact=" << local_summary.exact_safety_in_ms
                   << " ms, lb_violations=" << local_summary.occt_lower_bound_in_violations
                   << ", avg_lb_ratio=" << local_summary.avg_dti_lb_ratio
                   << ", avg_occt/g4=" << local_summary.avg_dti_g4_occt_ratio
                   << "; DTO: g4=" << local_summary.native_safety_out_ms
                   << " ms, occt=" << local_summary.imported_safety_out_ms
                   << " ms, exact=" << local_summary.exact_safety_out_ms
                   << " ms, lb_violations=" << local_summary.occt_lower_bound_out_violations
                   << ", avg_lb_ratio=" << local_summary.avg_dto_lb_ratio
                   << ", avg_occt/g4=" << local_summary.avg_dto_g4_occt_ratio;
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
