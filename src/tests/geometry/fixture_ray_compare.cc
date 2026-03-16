// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_ray_compare.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_solid_builder.hh"

#include <G4GeometryTolerance.hh>
#include <G4ThreeVector.hh>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
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

  RaySample TraceRay(const G4VSolid& solid, const G4ThreeVector& origin, const EInside state,
                     const G4ThreeVector& direction) {
    RaySample sample;
    if (state == kOutside) {
      sample.distance = solid.DistanceToIn(origin, direction);
    } else {
      sample.distance =
          solid.DistanceToOut(origin, direction, true, &sample.validNorm, &sample.normal);
    }
    sample.intersects = !std::isinf(sample.distance);
    return sample;
  }

  std::string DistanceString(const G4double value) {
    if (std::isinf(value)) {
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

  /// Write a JSON array of 3-vectors to an output stream.
  void WriteJsonVectorArray(std::ostream& out, const std::vector<G4ThreeVector>& points) {
    out << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
      if (index > 0U) {
        out << ',';
      }
      out << std::setprecision(15) << '[' << points[index].x() << ',' << points[index].y() << ','
          << points[index].z() << ']';
    }
    out << ']';
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
    std::ofstream out(output_path);
    if (!out) {
      throw std::runtime_error("Cannot open point-cloud output file: " + output_path.string());
    }
    out << "{\n";
    out << "  \"fixture_id\": " << JsonString(fixture_id) << ",\n";
    out << "  \"geant4_class\": " << JsonString(geant4_class) << ",\n";
    out << "  \"ray_count\": " << ray_count << ",\n";
    out << std::setprecision(15);
    out << "  \"native_pre_step_origin\": [" << native_origin.x() << ',' << native_origin.y() << ','
        << native_origin.z() << "],\n";
    out << "  \"imported_pre_step_origin\": [" << imported_origin.x() << ',' << imported_origin.y()
        << ',' << imported_origin.z() << "],\n";
    out << "  \"native_post_step_hits\": ";
    WriteJsonVectorArray(out, native_hits);
    out << ",\n";
    out << "  \"imported_post_step_hits\": ";
    WriteJsonVectorArray(out, imported_hits);
    out << "\n}\n";
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

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolid(provenance);
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

    std::ostringstream timing_summary;
    timing_summary << "Compared " << local_summary.ray_count << " rays for fixture '"
                   << request.fixture.id << "' (" << local_summary.geant4_class
                   << ") with tolerance " << local_summary.distance_tolerance
                   << " mm; native=" << local_summary.native_elapsed_ms
                   << " ms, imported=" << local_summary.imported_elapsed_ms
                   << " ms, mismatches=" << local_summary.mismatch_count
                   << ", normal_mismatches=" << local_summary.normal_mismatch_count;
    report.AddInfo("fixture.ray_compare_summary", timing_summary.str(), provenance_path);

    if (!options.point_cloud_dir.empty()) {
      std::vector<G4ThreeVector> native_hits;
      std::vector<G4ThreeVector> imported_hits;
      native_hits.reserve(directions.size());
      imported_hits.reserve(directions.size());
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
      filename += ".json";
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
