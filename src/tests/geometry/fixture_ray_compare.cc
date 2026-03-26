// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "geometry/fixture_ray_compare.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_solid_builder.hh"

#include <yaml-cpp/yaml.h>

#include <G4GeometryTolerance.hh>
#include <G4ThreeVector.hh>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace g4occt::tests::geometry {
namespace {

#ifndef G4OCCT_TEST_SOURCE_DIR
#define G4OCCT_TEST_SOURCE_DIR "."
#endif

  constexpr double kNormalAgreementThreshold = 1.0 - 1e-3; // dot product threshold (~2.6°)

  struct RaySample {
    bool intersects{false};
    G4double distance{kInfinity};
    G4ThreeVector normal;
    G4bool validNorm{false};
  };

  std::string ToString(const G4ThreeVector& vector) {
    std::ostringstream buffer;
    buffer << "(" << vector.x() << ", " << vector.y() << ", " << vector.z() << ")";
    return buffer.str();
  }

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

  /// One value of the Halton low-discrepancy sequence in the given prime base.
  /// index is 1-based so Halton(1, base) is the first value (in (0, 1)).
  double Halton(std::size_t index, std::size_t base) {
    double result = 0.0;
    double frac   = 1.0;
    while (index > 0) {
      frac /= static_cast<double>(base);
      result += frac * static_cast<double>(index % base);
      index /= base;
    }
    return result;
  }

  struct SurfaceRay {
    G4ThreeVector origin;    ///< Just outside the bounding-box face.
    G4ThreeVector direction; ///< Unit inward normal of the face.
  };

  /**
   * Generate n_per_face inward-bound rays across each of the six axis-aligned
   * bounding-box faces of solid using a 2-D Halton sequence (bases 2 and 3).
   * Origins are offset outward by the Geant4 surface tolerance so they start
   * in the kOutside region; directions are the inward face normals.
   */
  std::vector<SurfaceRay> GenerateSurfaceRays(const G4VSolid& solid, std::size_t n_per_face) {
    if (n_per_face == 0) {
      return {};
    }
    G4ThreeVector bb_min, bb_max;
    solid.BoundingLimits(bb_min, bb_max);
    const G4double margin = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();
    const G4double dx     = bb_max.x() - bb_min.x();
    const G4double dy     = bb_max.y() - bb_min.y();
    const G4double dz     = bb_max.z() - bb_min.z();

    std::vector<SurfaceRay> rays;
    rays.reserve(6 * n_per_face);

    for (std::size_t i = 1; i <= n_per_face; ++i) {
      const double u = Halton(i, 2); // first face-local coordinate
      const double v = Halton(i, 3); // second face-local coordinate

      // +x face (y-z plane), direction −x
      rays.push_back({G4ThreeVector(bb_max.x() + margin, bb_min.y() + u * dy, bb_min.z() + v * dz),
                      G4ThreeVector(-1, 0, 0)});
      // −x face, direction +x
      rays.push_back({G4ThreeVector(bb_min.x() - margin, bb_min.y() + u * dy, bb_min.z() + v * dz),
                      G4ThreeVector(1, 0, 0)});
      // +y face (x-z plane), direction −y
      rays.push_back({G4ThreeVector(bb_min.x() + u * dx, bb_max.y() + margin, bb_min.z() + v * dz),
                      G4ThreeVector(0, -1, 0)});
      // −y face, direction +y
      rays.push_back({G4ThreeVector(bb_min.x() + u * dx, bb_min.y() - margin, bb_min.z() + v * dz),
                      G4ThreeVector(0, 1, 0)});
      // +z face (x-y plane), direction −z
      rays.push_back({G4ThreeVector(bb_min.x() + u * dx, bb_min.y() + v * dy, bb_max.z() + margin),
                      G4ThreeVector(0, 0, -1)});
      // −z face, direction +z
      rays.push_back({G4ThreeVector(bb_min.x() + u * dx, bb_min.y() + v * dy, bb_min.z() - margin),
                      G4ThreeVector(0, 0, 1)});
    }
    return rays;
  }

  RaySample TraceRay(const G4VSolid& solid, const G4ThreeVector& origin, const EInside state,
                     const G4ThreeVector& direction) {
    RaySample sample;
    if (state == kOutside) {
      sample.distance = solid.DistanceToIn(origin, direction);
    } else {
      sample.distance =
          solid.DistanceToOut(origin, direction, true, &sample.validNorm, &sample.normal);
    }
    // Geant4's kInfinity is 9.9999e+99, a large finite double, not IEEE
    // infinity. std::isinf would return false for it, causing rays that miss
    // the solid (DistanceToIn = kInfinity) to be treated as valid hits.
    sample.intersects = !std::isinf(sample.distance) && sample.distance < kInfinity;
    return sample;
  }

  std::string DistanceString(const G4double value) {
    if (std::isinf(value) || value >= kInfinity) {
      return "kInfinity";
    }
    std::ostringstream buffer;
    buffer << std::setprecision(16) << value;
    return buffer.str();
  }

  /// Escape a string for safe embedding as a JSON string value.
  /// Handles `"`, `\`, and all control characters (code points < U+0020).
  std::string JsonString(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2U);
    result += '"';
    for (const char ch : s) {
      const auto uc = static_cast<unsigned char>(ch);
      if (ch == '"') {
        result += "\\\"";
      } else if (ch == '\\') {
        result += "\\\\";
      } else if (uc < 0x20U) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04X", uc);
        result += buf;
      } else {
        result += ch;
      }
    }
    result += '"';
    return result;
  }

  /**
   * Write a JSON file with the pre-step origins and the post-step surface hit
   * points collected during native-vs-imported ray comparison.
   *
   * Each entry in native_hits / imported_hits is
   *   origin + distance * direction
   * for rays that intersect the respective solid.  The two solids may use
   * slightly different launch points (FixtureComparisonOrigin adapts per
   * solid), so both origins are recorded separately.
   */
  void WritePointCloudJson(const std::filesystem::path& output_path, const std::string& fixture_id,
                           const std::string& geant4_class, const std::size_t ray_count,
                           const G4ThreeVector& native_origin, const G4ThreeVector& imported_origin,
                           const std::vector<G4ThreeVector>& native_hits,
                           const std::vector<G4ThreeVector>& imported_hits) {
    // Use output_path.string() so gzopen receives a char* on all platforms.
    const std::string path_str = output_path.string();
    gzFile gz                  = gzopen(path_str.c_str(), "wb");
    if (!gz) {
      throw std::runtime_error("Cannot open point-cloud output file: " + path_str);
    }

    // Write a string chunk to gz; guards against INT_MAX overflow (gzwrite return type is int).
    auto write_gz = [&gz, &path_str](const std::string& s) {
      if (s.empty()) {
        return;
      }
      if (s.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Chunk too large for gzwrite: " + path_str);
      }
      const int written = gzwrite(gz, s.data(), static_cast<unsigned int>(s.size()));
      if (written != static_cast<int>(s.size())) {
        throw std::runtime_error("Failed to write gzip data to: " + path_str);
      }
    };

    // Write a JSON array of 3-vectors incrementally, one point at a time, to
    // avoid buffering the entire array in memory before compressing.
    auto write_gz_vector_array = [&write_gz](const std::vector<G4ThreeVector>& points) {
      write_gz("[");
      for (std::size_t i = 0; i < points.size(); ++i) {
        if (i > 0) {
          write_gz(",");
        }
        std::ostringstream oss;
        oss << std::setprecision(15) << '[' << points[i].x() << ',' << points[i].y() << ','
            << points[i].z() << ']';
        write_gz(oss.str());
      }
      write_gz("]");
    };

    try {
      std::ostringstream header;
      header << "{\n";
      header << "  \"fixture_id\": " << JsonString(fixture_id) << ",\n";
      header << "  \"geant4_class\": " << JsonString(geant4_class) << ",\n";
      header << "  \"ray_count\": " << ray_count << ",\n";
      header << std::setprecision(15);
      header << "  \"native_pre_step_origin\": [" << native_origin.x() << ',' << native_origin.y()
             << ',' << native_origin.z() << "],\n";
      header << "  \"imported_pre_step_origin\": [" << imported_origin.x() << ','
             << imported_origin.y() << ',' << imported_origin.z() << "],\n";
      header << "  \"native_post_step_hits\": ";
      write_gz(header.str());
      write_gz_vector_array(native_hits);
      write_gz(",\n  \"imported_post_step_hits\": ");
      write_gz_vector_array(imported_hits);
      write_gz("\n}\n");
    } catch (...) {
      gzclose(gz);
      throw;
    }

    const int close_ret = gzclose(gz);
    if (close_ret != Z_OK) {
      throw std::runtime_error("Failed to finalise gzip file: " + path_str);
    }
  }

} // namespace

std::filesystem::path DefaultRepositoryManifestPath() {
  return std::filesystem::path(G4OCCT_TEST_SOURCE_DIR) / "fixtures" / "geometry" / "manifest.yaml";
}

G4double DefaultRayComparisonTolerance() {
  return G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();
}

ValidationReport CompareFixtureRays(const FixtureValidationRequest& request,
                                    const FixtureRayComparisonOptions& options,
                                    FixtureRayComparisonSummary* summary) {
  ValidationReport report;
  if (options.ray_count == 0U) {
    report.AddError("fixture.ray_compare_invalid_count", "Ray count must be positive",
                    request.manifest.source_path);
    return report;
  }

  FixtureRayComparisonSummary local_summary;
  local_summary.fixture_id         = request.manifest.family + "/" + request.fixture.id;
  local_summary.distance_tolerance = DefaultRayComparisonTolerance();

  try {
    const auto provenance_path = ResolveFixtureProvenancePath(request.manifest, request.fixture);
    const FixtureProvenance provenance = ParseFixtureProvenance(provenance_path);
    local_summary.geant4_class         = Geant4Class(provenance);

    const YAML::Node validation = provenance.document["validation"];
    if (validation.IsDefined() && validation["distance_tolerance_mm"].IsDefined()) {
      const double configured_tolerance = validation["distance_tolerance_mm"].as<double>();
      if (!std::isfinite(configured_tolerance) || configured_tolerance <= 0.0) {
        std::ostringstream message;
        message << "Invalid validation.distance_tolerance_mm=" << configured_tolerance
                << " for fixture '" << request.fixture.id
                << "': tolerance must be finite and > 0 mm.";
        report.AddError("fixture.invalid_distance_tolerance_mm", message.str(), provenance_path);
      } else {
        local_summary.distance_tolerance = configured_tolerance;
      }
    }

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolidForRequest(request, provenance);
    auto imported_solid =
        std::make_unique<G4OCCTSolid>(request.fixture.id + "_imported", LoadImportedShape(request));

    local_summary.native_origin         = FixtureComparisonOrigin(provenance, *native_solid);
    local_summary.imported_origin       = FixtureComparisonOrigin(provenance, *imported_solid);
    local_summary.native_origin_state   = native_solid->Inside(local_summary.native_origin);
    local_summary.imported_origin_state = imported_solid->Inside(local_summary.imported_origin);

    if (local_summary.native_origin_state != local_summary.imported_origin_state) {
      report.AddError("fixture.ray_origin_state_mismatch",
                      "Comparison origin classification mismatch for fixture '" +
                          request.fixture.id +
                          "': native=" + ToString(local_summary.native_origin_state) + " at " +
                          ToString(local_summary.native_origin) +
                          ", imported=" + ToString(local_summary.imported_origin_state) + " at " +
                          ToString(local_summary.imported_origin),
                      provenance_path);
    }

    const std::vector<G4ThreeVector> directions = GenerateDirections(options.ray_count);
    local_summary.ray_count                     = directions.size();
    std::vector<RaySample> native_samples;
    std::vector<RaySample> imported_samples;
    native_samples.reserve(directions.size());
    imported_samples.reserve(directions.size());

    const auto native_begin = std::chrono::steady_clock::now();
    for (const auto& direction : directions) {
      native_samples.push_back(TraceRay(*native_solid, local_summary.native_origin,
                                        local_summary.native_origin_state, direction));
    }
    const auto native_end = std::chrono::steady_clock::now();
    local_summary.native_elapsed_ms =
        std::chrono::duration<double, std::milli>(native_end - native_begin).count();

    const auto imported_begin = std::chrono::steady_clock::now();
    for (const auto& direction : directions) {
      imported_samples.push_back(TraceRay(*imported_solid, local_summary.imported_origin,
                                          local_summary.imported_origin_state, direction));
    }
    const auto imported_end = std::chrono::steady_clock::now();
    local_summary.imported_elapsed_ms =
        std::chrono::duration<double, std::milli>(imported_end - imported_begin).count();

    for (std::size_t index = 0; index < directions.size(); ++index) {
      const RaySample& native_sample   = native_samples[index];
      const RaySample& imported_sample = imported_samples[index];
      if (native_sample.intersects != imported_sample.intersects) {
        ++local_summary.mismatch_count;
        if (local_summary.mismatch_count <= options.max_reported_mismatches) {
          report.AddError("fixture.ray_intersection_mismatch",
                          "Ray " + std::to_string(index) + " hit mismatch for fixture '" +
                              request.fixture.id +
                              "': native=" + (native_sample.intersects ? "hit" : "miss") +
                              ", imported=" + (imported_sample.intersects ? "hit" : "miss") +
                              ", direction=" + ToString(directions[index]),
                          provenance_path);
        }
        continue;
      }
      if (!native_sample.intersects) {
        continue;
      }

      const G4double distance_delta = std::fabs(native_sample.distance - imported_sample.distance);
      if (distance_delta > local_summary.distance_tolerance) {
        ++local_summary.mismatch_count;
        if (local_summary.mismatch_count <= options.max_reported_mismatches) {
          std::ostringstream message;
          message << "Ray " << index << " distance mismatch for fixture '" << request.fixture.id
                  << "': native=" << DistanceString(native_sample.distance)
                  << ", imported=" << DistanceString(imported_sample.distance)
                  << ", |delta|=" << distance_delta
                  << ", tolerance=" << local_summary.distance_tolerance
                  << ", direction=" << ToString(directions[index]);
          report.AddError("fixture.ray_distance_mismatch", message.str(), provenance_path);
        }
        continue;
      }

      if (native_sample.validNorm && imported_sample.validNorm) {
        const G4double normal_dot = native_sample.normal.dot(imported_sample.normal);
        if (normal_dot < kNormalAgreementThreshold) {
          ++local_summary.normal_mismatch_count;
          ++local_summary.mismatch_count;
          if (local_summary.mismatch_count <= options.max_reported_mismatches) {
            std::ostringstream message;
            message << "Ray " << index << " normal mismatch for fixture '" << request.fixture.id
                    << "': native=" << ToString(native_sample.normal)
                    << ", imported=" << ToString(imported_sample.normal) << ", dot=" << normal_dot
                    << ", direction=" << ToString(directions[index]);
            report.AddError("fixture.ray_normal_mismatch", message.str(), provenance_path);
          }
        }
      }
    }

    // ── Surface-normal benchmark at ray hit points ────────────────────────
    // Collect agreed hit points: rays where both solids agree on intersection
    // and the distance is within tolerance.
    /// A ray hit point where both native and imported solids agreed on
    /// intersection and distance (within tolerance).
    struct AgreedHit {
      std::size_t ray_index;
      G4ThreeVector native_point;
      G4ThreeVector imported_point;
    };
    std::vector<AgreedHit> agreed_hits;
    agreed_hits.reserve(directions.size());

    for (std::size_t index = 0; index < directions.size(); ++index) {
      const RaySample& native_sample   = native_samples[index];
      const RaySample& imported_sample = imported_samples[index];
      if (!native_sample.intersects || !imported_sample.intersects) {
        continue;
      }
      const G4double distance_delta = std::fabs(native_sample.distance - imported_sample.distance);
      if (distance_delta > local_summary.distance_tolerance) {
        continue;
      }
      agreed_hits.push_back(
          {index, local_summary.native_origin + native_sample.distance * directions[index],
           local_summary.imported_origin + imported_sample.distance * directions[index]});
    }
    local_summary.surface_normal_count = agreed_hits.size();

    // ── Bounding-box surface rays ─────────────────────────────────────────────
    // Fire n_per_face inward-bound rays per bounding-box face to expose interior
    // surface patches that central rays miss in non-convex solids (torus, NIST CTC).
    // Origins come from the native solid's bounding box and the same origin/direction
    // is used for both solids so distance comparisons remain fair.
    // Hit points feed the point cloud but NOT the SurfaceNormal benchmark: axis-aligned
    // rays frequently land on edges where the outward normal is geometrically ambiguous.
    const std::vector<SurfaceRay> surface_rays =
        GenerateSurfaceRays(*native_solid, options.surface_rays_per_face);
    local_summary.surface_ray_count = surface_rays.size();

    std::vector<G4double> native_surface_dists(surface_rays.size());
    std::vector<G4double> imported_surface_dists(surface_rays.size());
    for (std::size_t i = 0; i < surface_rays.size(); ++i) {
      native_surface_dists[i]   = native_solid->DistanceToIn(surface_rays[i].origin, surface_rays[i].direction);
      imported_surface_dists[i] = imported_solid->DistanceToIn(surface_rays[i].origin, surface_rays[i].direction);
    }

    for (std::size_t i = 0; i < surface_rays.size(); ++i) {
      const G4double nd = native_surface_dists[i];
      const G4double id = imported_surface_dists[i];
      const bool native_hit   = !std::isinf(nd) && nd < kInfinity;
      const bool imported_hit = !std::isinf(id) && id < kInfinity;

      if (native_hit != imported_hit) {
        ++local_summary.mismatch_count;
        if (local_summary.mismatch_count <= options.max_reported_mismatches) {
          report.AddError(
              "fixture.surface_ray_intersection_mismatch",
              "Surface ray " + std::to_string(i) + " hit mismatch for fixture '" +
                  request.fixture.id + "': native=" + (native_hit ? "hit" : "miss") +
                  ", imported=" + (imported_hit ? "hit" : "miss") +
                  ", origin=" + ToString(surface_rays[i].origin) +
                  ", direction=" + ToString(surface_rays[i].direction),
              provenance_path);
        }
        continue;
      }
      if (!native_hit) {
        continue;
      }

      const G4double delta = std::fabs(nd - id);
      if (delta > local_summary.distance_tolerance) {
        ++local_summary.mismatch_count;
        if (local_summary.mismatch_count <= options.max_reported_mismatches) {
          std::ostringstream message;
          message << "Surface ray " << i << " distance mismatch for fixture '"
                  << request.fixture.id << "': native=" << DistanceString(nd)
                  << ", imported=" << DistanceString(id) << ", |delta|=" << delta
                  << ", tolerance=" << local_summary.distance_tolerance
                  << ", origin=" << ToString(surface_rays[i].origin)
                  << ", direction=" << ToString(surface_rays[i].direction);
          report.AddError("fixture.surface_ray_distance_mismatch", message.str(), provenance_path);
        }
        continue;
      }
    }

    // Time native SurfaceNormal(p) calls.
    std::vector<G4ThreeVector> native_surface_normals(agreed_hits.size());
    const auto sn_native_begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < agreed_hits.size(); ++i) {
      native_surface_normals[i] = native_solid->SurfaceNormal(agreed_hits[i].native_point);
    }
    const auto sn_native_end = std::chrono::steady_clock::now();
    local_summary.native_surface_normal_ms =
        std::chrono::duration<double, std::milli>(sn_native_end - sn_native_begin).count();

    // Time imported SurfaceNormal(p) calls.
    std::vector<G4ThreeVector> imported_surface_normals(agreed_hits.size());
    const auto sn_imported_begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < agreed_hits.size(); ++i) {
      imported_surface_normals[i] = imported_solid->SurfaceNormal(agreed_hits[i].imported_point);
    }
    const auto sn_imported_end = std::chrono::steady_clock::now();
    local_summary.imported_surface_normal_ms =
        std::chrono::duration<double, std::milli>(sn_imported_end - sn_imported_begin).count();

    // Compare native vs imported SurfaceNormal(p).
    std::size_t reported_sn_mismatches = 0;
    for (std::size_t i = 0; i < agreed_hits.size(); ++i) {
      const G4double dot = native_surface_normals[i].dot(imported_surface_normals[i]);
      if (dot < kNormalAgreementThreshold) {
        ++local_summary.surface_normal_mismatch_count;
        if (reported_sn_mismatches < options.max_reported_mismatches) {
          ++reported_sn_mismatches;
          const std::size_t ray_index = agreed_hits[i].ray_index;
          std::ostringstream message;
          message << "Ray " << ray_index << " SurfaceNormal mismatch for fixture '"
                  << request.fixture.id << "': native=" << ToString(native_surface_normals[i])
                  << " at native_point=" << ToString(agreed_hits[i].native_point)
                  << ", imported=" << ToString(imported_surface_normals[i])
                  << " at imported_point=" << ToString(agreed_hits[i].imported_point)
                  << ", dot=" << dot;
          report.AddError("fixture.surface_normal_mismatch", message.str(), provenance_path);
        }
      }
    }

    // Cross-validate: for rays where DistanceToOut provided a valid exit normal,
    // verify that SurfaceNormal(hit_point) agrees (internal consistency check).
    // This check is performed on both native and imported solids independently.
    auto cross_validate = [&](const std::string& label, const std::vector<RaySample>& samples,
                              const std::vector<G4ThreeVector>& sn_normals) {
      for (std::size_t i = 0; i < agreed_hits.size(); ++i) {
        const std::size_t ray_index = agreed_hits[i].ray_index;
        const RaySample& sample     = samples[ray_index];
        if (!sample.validNorm) {
          continue;
        }
        const G4double dot = sn_normals[i].dot(sample.normal);
        if (dot < kNormalAgreementThreshold) {
          std::ostringstream message;
          message << "Ray " << ray_index
                  << " SurfaceNormal vs DistanceToOut cross-validation mismatch (" << label
                  << ") for fixture '" << request.fixture.id
                  << "': SurfaceNormal=" << ToString(sn_normals[i])
                  << ", DistanceToOut normal=" << ToString(sample.normal) << ", dot=" << dot;
          report.AddInfo("fixture.surface_normal_crossval_mismatch", message.str(),
                         provenance_path);
        }
      }
    };
    cross_validate("native", native_samples, native_surface_normals);
    cross_validate("imported", imported_samples, imported_surface_normals);

    std::ostringstream timing_summary;
    timing_summary << "Compared " << local_summary.ray_count << " rays for fixture '"
                   << request.fixture.id << "' (" << local_summary.geant4_class
                   << ") with tolerance " << local_summary.distance_tolerance
                   << " mm; native=" << local_summary.native_elapsed_ms
                   << " ms, imported=" << local_summary.imported_elapsed_ms
                   << " ms, mismatches=" << local_summary.mismatch_count
                   << ", normal_mismatches=" << local_summary.normal_mismatch_count
                   << "; surface_rays=" << local_summary.surface_ray_count
                   << "; SurfaceNormal(" << local_summary.surface_normal_count
                   << " points): native=" << local_summary.native_surface_normal_ms
                   << " ms, imported=" << local_summary.imported_surface_normal_ms
                   << " ms, mismatches=" << local_summary.surface_normal_mismatch_count;
    report.AddInfo("fixture.ray_compare_summary", timing_summary.str(), provenance_path);

    if (!options.point_cloud_dir.empty()) {
      std::vector<G4ThreeVector> native_hits;
      std::vector<G4ThreeVector> imported_hits;
      native_hits.reserve(directions.size() + surface_rays.size());
      imported_hits.reserve(directions.size() + surface_rays.size());
      for (std::size_t index = 0; index < directions.size(); ++index) {
        if (native_samples[index].intersects) {
          native_hits.push_back(local_summary.native_origin +
                                native_samples[index].distance * directions[index]);
        }
        if (imported_samples[index].intersects) {
          imported_hits.push_back(local_summary.imported_origin +
                                  imported_samples[index].distance * directions[index]);
        }
      }
      for (std::size_t i = 0; i < surface_rays.size(); ++i) {
        if (!std::isinf(native_surface_dists[i]) && native_surface_dists[i] < kInfinity) {
          native_hits.push_back(surface_rays[i].origin +
                                native_surface_dists[i] * surface_rays[i].direction);
        }
        if (!std::isinf(imported_surface_dists[i]) && imported_surface_dists[i] < kInfinity) {
          imported_hits.push_back(surface_rays[i].origin +
                                  imported_surface_dists[i] * surface_rays[i].direction);
        }
      }
      // Derive a safe flat filename from the qualified fixture ID.
      // Replace every character that is not alphanumeric, a hyphen, or a
      // period with an underscore so the result is valid on all platforms.
      std::string filename = local_summary.fixture_id;
      for (char& c : filename) {
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '-' &&
            c != '.') {
          c = '_';
        }
      }
      filename += ".json.gz";
      std::filesystem::create_directories(options.point_cloud_dir);
      WritePointCloudJson(options.point_cloud_dir / filename, local_summary.fixture_id,
                          local_summary.geant4_class, local_summary.ray_count,
                          local_summary.native_origin, local_summary.imported_origin, native_hits,
                          imported_hits);
    }
  } catch (const std::exception& error) {
    report.AddError("fixture.ray_compare_failed",
                    std::string("Fixture ray comparison failed: ") + error.what(),
                    ResolveFixtureProvenancePath(request.manifest, request.fixture));
  }

  if (summary != nullptr) {
    *summary = local_summary;
  }
  return report;
}

} // namespace g4occt::tests::geometry
