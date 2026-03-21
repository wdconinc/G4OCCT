// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file bench_assembly_navigator.cc
/// @brief Benchmark comparing GDML vs STEP assembly ray-casting.
///
/// For each fixture directory under assembly-comparison that contains both
/// geometry.gdml and shape.step, loads the same assembly in two representations:
///   - GDML reference: solids placed in a world volume parsed from geometry.gdml.
///   - STEP import:    G4OCCTAssemblyVolume::FromSTEP, yielding G4OCCTSolid
///                     objects at the same positions.
///
/// Per-fixture material mapping is loaded from materials.xml when present;
/// fixtures without materials.xml fall back to Component→G4_Al (legacy default).
/// The STEP world volume dimensions are derived from the GDML world box so that
/// fixtures of any size are accommodated automatically.
///
/// For each ray direction sampled from the unit sphere the benchmark
/// computes all volume-boundary crossing distances through each
/// representation and compares them.  Boundary-crossing points are
/// collected for the point-cloud viewer, which can be shared with the
/// per-solid benchmark output.
///
/// Usage:
///   bench_assembly_navigator [N_rays] [fixture_root] [point_cloud_dir]
///
/// fixture_root may be either:
///   - The assembly-comparison root directory (containing per-fixture
///     subdirectories); all subdirectories are registered as benchmarks.
///   - A single fixture directory (one that directly contains geometry.gdml
///     and shape.step); only that fixture is benchmarked.  CI uses this
///     single-fixture mode, invoking the binary once per fixture so that
///     each run gets a clean Geant4 global state (material table, solid
///     store, logical-volume store) and no cross-fixture name conflicts can
///     occur.
///
/// Defaults:
///   N_rays         = 2048
///   fixture_root   = <source tree>/src/tests/fixtures/assembly-comparison
///   point_cloud_dir = "" (no point-cloud output)

// Callgrind instrumentation macros.  When the binary runs outside valgrind, or
// when the valgrind headers are absent at build time, these expand to no-ops.
#ifdef HAVE_VALGRIND_CALLGRIND_H
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_START_INSTRUMENTATION                                                            \
  do {                                                                                             \
  } while (0)
#define CALLGRIND_STOP_INSTRUMENTATION                                                             \
  do {                                                                                             \
  } while (0)
#define CALLGRIND_TOGGLE_COLLECT                                                                   \
  do {                                                                                             \
  } while (0)
#endif

#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"
#include "G4OCCT/G4OCCTMaterialMapReader.hh"

#include <G4Box.hh>
#include <G4GDMLParser.hh>
#include <G4GeometryTolerance.hh>
#include <G4LogicalVolume.hh>
#include <G4Material.hh>
#include <G4NistManager.hh>
#include <G4RotationMatrix.hh>
#include <G4ThreeVector.hh>
#include <G4VPhysicalVolume.hh>
#include <G4VSolid.hh>

#include <benchmark/benchmark.h>

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace g4occt::benchmarks::assembly {

// ─── Default fixture root (relative to the source tree) ────────────────────────
// Must be outside the anonymous namespace so main() can call it via the
// qualified name g4occt::benchmarks::assembly::DefaultAssemblyFixtureRoot().

#ifndef G4OCCT_TEST_SOURCE_DIR
#define G4OCCT_TEST_SOURCE_DIR "."
#endif

std::filesystem::path DefaultAssemblyFixtureRoot() {
  return std::filesystem::path(G4OCCT_TEST_SOURCE_DIR) / "fixtures" / "assembly-comparison";
}

namespace {

  // ─── Component specification ──────────────────────────────────────────────────

  /// One placed component in an assembly: a solid plus its world-frame transform.
  struct ComponentSpec {
    G4VSolid* solid{nullptr};
    G4ThreeVector translation;
    G4RotationMatrix rotation; // identity for our axis-aligned boxes
    std::string name;
    G4Material* material{nullptr};
  };

  // ─── Boundary-crossing record ─────────────────────────────────────────────────

  /// A single surface-boundary crossing point collected during ray tracing.
  struct Crossing {
    double distance{0.0}; ///< Along the ray from the launch origin.
    G4ThreeVector point;  ///< World-frame coordinates.
  };

  // ─── Ray-direction generation (Fibonacci sphere) ──────────────────────────────

  /// Return @p n ray directions uniformly distributed on the unit sphere using
  /// the Fibonacci / golden-angle lattice.  The same sequence is used for every
  /// run, making benchmark timing reproducible across iterations.
  std::vector<G4ThreeVector> GenerateDirections(std::size_t n) {
    std::vector<G4ThreeVector> dirs;
    dirs.reserve(n);
    constexpr double kGoldenAngle = 2.399963229728653; // 2π(1 − 1/φ)
    for (std::size_t i = 0; i < n; ++i) {
      const double phi =
          std::acos(1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(n));
      const double theta = kGoldenAngle * static_cast<double>(i);
      dirs.emplace_back(std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta),
                        std::cos(phi));
    }
    return dirs;
  }

  // ─── Ray-through-assembly tracing ─────────────────────────────────────────────

  /// Compute all boundary-crossing distances for a ray through the assembly.
  ///
  /// Each component is tested individually.  For a ray that starts outside the
  /// component solid (kOutside) the function calls DistanceToIn; if the ray
  /// enters, a subsequent DistanceToOut gives the exit distance.  Crossings are
  /// returned sorted by ascending distance from @p origin.
  std::vector<Crossing> TraceRay(const std::vector<ComponentSpec>& components,
                                 const G4ThreeVector& origin, const G4ThreeVector& direction) {
    std::vector<Crossing> crossings;
    crossings.reserve(components.size() * 2U);

    for (const auto& comp : components) {
      // Transform the ray into the component's local frame.
      // For our fixture all boxes are axis-aligned so the rotation is identity.
      const G4ThreeVector local_origin = comp.rotation.inverse() * (origin - comp.translation);
      const G4ThreeVector local_dir    = comp.rotation.inverse() * direction;

      const EInside state = comp.solid->Inside(local_origin);

      if (state == kOutside) {
        const G4double dist_in = comp.solid->DistanceToIn(local_origin, local_dir);
        if (dist_in >= 0.5 * kInfinity) {
          continue;
        }
        const G4ThreeVector entry_world = origin + dist_in * direction;
        crossings.push_back({dist_in, entry_world});

        const G4ThreeVector local_entry = local_origin + dist_in * local_dir;
        const G4double dist_out =
            comp.solid->DistanceToOut(local_entry, local_dir, false, nullptr, nullptr);
        if (dist_out < 0.5 * kInfinity) {
          const G4ThreeVector exit_world = origin + (dist_in + dist_out) * direction;
          crossings.push_back({dist_in + dist_out, exit_world});
        }
      } else if (state == kInside) {
        const G4double dist_out =
            comp.solid->DistanceToOut(local_origin, local_dir, false, nullptr, nullptr);
        if (dist_out < 0.5 * kInfinity) {
          const G4ThreeVector exit_world = origin + dist_out * direction;
          crossings.push_back({dist_out, exit_world});
        }
      }
      // kSurface: skip — boundary origin is ambiguous for both solids.
    }

    std::sort(crossings.begin(), crossings.end(),
              [](const Crossing& a, const Crossing& b) { return a.distance < b.distance; });
    return crossings;
  }

  // ─── Component extraction helpers ────────────────────────────────────────────

  /// Extract G4VSolid + world-frame transforms + material from all daughter volumes of @p lv.
  std::vector<ComponentSpec> ExtractComponents(G4LogicalVolume* lv) {
    std::vector<ComponentSpec> specs;
    const int n = static_cast<int>(lv->GetNoDaughters());
    specs.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const G4VPhysicalVolume* child = lv->GetDaughter(i);
      ComponentSpec spec;
      spec.solid                  = child->GetLogicalVolume()->GetSolid();
      spec.translation            = child->GetTranslation();
      const G4RotationMatrix* rot = child->GetFrameRotation();
      if (rot != nullptr) {
        spec.rotation = *rot;
      }
      spec.name     = child->GetName();
      spec.material = child->GetLogicalVolume()->GetMaterial();
      specs.push_back(spec);
    }
    return specs;
  }

  // ─── Point-cloud JSON output ─────────────────────────────────────────────────

  /// Escape @p s for embedding as a JSON string value.
  std::string JsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2U);
    out += '"';
    for (const char ch : s) {
      const auto uc = static_cast<unsigned char>(ch);
      if (ch == '"') {
        out += "\\\"";
      } else if (ch == '\\') {
        out += "\\\\";
      } else if (uc < 0x20U) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04X", uc);
        out += buf;
      } else {
        out += ch;
      }
    }
    out += '"';
    return out;
  }

  /// Write a gzip-compressed JSON point-cloud file compatible with the
  /// generate_point_cloud_report.py viewer.
  void WritePointCloud(const std::filesystem::path& output_path, const std::string& fixture_id,
                       std::size_t ray_count, const std::vector<G4ThreeVector>& gdml_hits,
                       const std::vector<G4ThreeVector>& step_hits) {
    std::filesystem::create_directories(output_path.parent_path());

    const std::string path_str = output_path.string();
    gzFile gz                  = gzopen(path_str.c_str(), "wb");
    if (gz == nullptr) {
      throw std::runtime_error("Cannot open point-cloud output: " + path_str);
    }

    auto write_gz = [&gz, &path_str](const std::string& s) {
      if (s.empty()) {
        return;
      }
      if (s.size() > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        throw std::runtime_error("Chunk too large for gzwrite: " + path_str);
      }
      const int written = gzwrite(gz, s.data(), static_cast<unsigned int>(s.size()));
      if (written != static_cast<int>(s.size())) {
        throw std::runtime_error("gzwrite failed: " + path_str);
      }
    };

    auto write_vec_array = [&write_gz](const std::vector<G4ThreeVector>& pts) {
      write_gz("[");
      for (std::size_t i = 0; i < pts.size(); ++i) {
        if (i > 0U) {
          write_gz(",");
        }
        std::ostringstream oss;
        oss << std::setprecision(15) << '[' << pts[i].x() << ',' << pts[i].y() << ',' << pts[i].z()
            << ']';
        write_gz(oss.str());
      }
      write_gz("]");
    };

    try {
      // Use the assembly centre (origin) as the shared pre-step origin so the
      // viewer can draw crossing points relative to a single reference point.
      std::ostringstream hdr;
      hdr << "{\n";
      hdr << "  \"fixture_id\": " << JsonString(fixture_id) << ",\n";
      hdr << "  \"geant4_class\": " << JsonString("G4OCCTAssemblyVolume") << ",\n";
      hdr << "  \"ray_count\": " << ray_count << ",\n";
      hdr << "  \"native_pre_step_origin\": [0,0,0],\n";
      hdr << "  \"imported_pre_step_origin\": [0,0,0],\n";
      hdr << "  \"native_post_step_hits\": ";
      write_gz(hdr.str());
      write_vec_array(gdml_hits);
      write_gz(",\n  \"imported_post_step_hits\": ");
      write_vec_array(step_hits);
      write_gz("\n}\n");
    } catch (...) {
      gzclose(gz);
      throw;
    }

    if (gzclose(gz) != Z_OK) {
      throw std::runtime_error("gzclose failed: " + path_str);
    }
  }

  // ─── Comparison ──────────────────────────────────────────────────────────────

  /// Compare two crossing sequences for a single ray.  Returns the mismatch
  /// count contribution: counts count differences and per-crossing distance
  /// mismatches beyond @p tolerance.
  std::size_t CompareRayCrossings(const std::vector<Crossing>& gdml,
                                  const std::vector<Crossing>& step, G4double tolerance) {
    if (gdml.size() != step.size()) {
      // Count mismatch is one error per extra/missing crossing.
      return std::max(gdml.size(), step.size()) - std::min(gdml.size(), step.size());
    }
    std::size_t mismatches = 0U;
    for (std::size_t i = 0; i < gdml.size(); ++i) {
      if (std::fabs(gdml[i].distance - step[i].distance) > tolerance) {
        ++mismatches;
      }
    }
    return mismatches;
  }

  /// Count the number of material mismatches between @p step and @p gdml
  /// component lists using a greedy one-to-one nearest-neighbour matching.
  ///
  /// Each STEP component is matched to the nearest (by Euclidean translation
  /// distance) *unmatched* GDML component.  Once a GDML component is claimed
  /// it cannot be matched again, preventing many-to-one assignments that would
  /// otherwise mask missing or duplicate components.  A mismatch is counted
  /// when:
  ///   - the matched GDML component carries a null material pointer, or
  ///   - the STEP component carries a null material pointer, or
  ///   - the material names differ, or
  ///   - no unmatched GDML component remains for a STEP component (STEP has
  ///     more components than GDML).
  /// Any GDML components left unmatched after all STEP components have been
  /// processed (GDML has more components than STEP) are also counted as
  /// mismatches.  Position mismatches are reported separately via
  /// CompareRayCrossings.
  std::size_t CountMaterialMismatches(const std::vector<ComponentSpec>& gdml,
                                      const std::vector<ComponentSpec>& step) {
    if (gdml.empty()) {
      // Nothing to match against; every STEP component is a mismatch.
      return step.size();
    }
    std::vector<bool> gdml_matched(gdml.size(), false);
    std::size_t mismatches = 0U;
    for (const auto& s : step) {
      double min_dist2     = std::numeric_limits<double>::max();
      std::size_t best_idx = gdml.size(); // sentinel: no unmatched candidate found
      for (std::size_t i = 0; i < gdml.size(); ++i) {
        if (gdml_matched[i]) {
          continue;
        }
        const double d2 = (s.translation - gdml[i].translation).mag2();
        if (d2 < min_dist2) {
          min_dist2 = d2;
          best_idx  = i;
        }
      }
      if (best_idx == gdml.size()) {
        // All GDML components already matched; excess STEP component.
        ++mismatches;
        continue;
      }
      gdml_matched[best_idx] = true;
      const ComponentSpec& g = gdml[best_idx];
      if (g.material == nullptr || s.material == nullptr ||
          g.material->GetName() != s.material->GetName()) {
        ++mismatches;
      }
    }
    // Any GDML components that were never matched (GDML has more than STEP).
    for (const bool matched : gdml_matched) {
      if (!matched) {
        ++mismatches;
      }
    }
    return mismatches;
  }

  // ─── Shared benchmark state ───────────────────────────────────────────────────

  struct BenchmarkResult {
    std::string fixture_id;
    double gdml_ms{0.0};
    double step_ms{0.0};
    std::size_t ray_count{0U};
    std::size_t mismatches{0U};
    std::size_t material_mismatches{0U};
    std::size_t gdml_crossings{0U};
    std::size_t step_crossings{0U};
  };

  struct SharedState {
    std::vector<BenchmarkResult> results;
    std::mutex mu;
  };

  static SharedState* g_state = // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
      nullptr;

  // ─── Benchmark registration ───────────────────────────────────────────────────

  void RunAssemblyBenchmark(benchmark::State& state, const std::string& fixture_id,
                            const std::filesystem::path& gdml_path,
                            const std::filesystem::path& step_path, std::size_t ray_count,
                            const std::filesystem::path& point_cloud_dir) {
    // Load GDML reference geometry.
    // Pass validate=false: G4GDMLParser::SetValidate() was removed in Geant4
    // 11.3; the validation flag is now passed directly to Read().
    G4GDMLParser gdml_parser;
    gdml_parser.Read(gdml_path.string(), /*validate=*/false);
    G4VPhysicalVolume* gdml_world_pv                 = gdml_parser.GetWorldVolume();
    G4LogicalVolume* gdml_world_lv                   = gdml_world_pv->GetLogicalVolume();
    const std::vector<ComponentSpec> gdml_components = ExtractComponents(gdml_world_lv);

    // Load STEP assembly.
    // Build the material map: use per-fixture materials.xml when present,
    // otherwise warn and fall back to the legacy default (Component → G4_Al).
    // The legacy default exists solely for fixtures predating materials.xml support;
    // new fixtures should always provide materials.xml.
    G4OCCTMaterialMap mat_map;
    const std::filesystem::path mat_xml_path = gdml_path.parent_path() / "materials.xml";
    if (std::filesystem::exists(mat_xml_path)) {
      G4OCCTMaterialMapReader reader;
      mat_map = reader.ReadFile(mat_xml_path.string());
    } else {
      std::cerr << "Warning: no materials.xml found for fixture '" << fixture_id
                << "'; applying legacy default (Component → G4_Al).\n";
      mat_map.Add("Component", G4NistManager::Instance()->FindOrBuildMaterial("G4_Al"));
    }

    // Derive the STEP world box half-lengths from the GDML world volume solid
    // so that any fixture size is accommodated automatically.  All assembly-
    // comparison fixtures use a G4Box as the world solid; warn and fall back to
    // the original 25×15×15 mm half-length defaults (i.e. 50×30×30 mm full extents)
    // if the cast ever fails.
    const auto* gdml_world_box = dynamic_cast<const G4Box*>(gdml_world_lv->GetSolid());
    if (!gdml_world_box) {
      std::cerr << "Warning: GDML world solid for fixture '" << fixture_id
                << "' is not a G4Box; using default 25×15×15 mm half-lengths for STEP world box.\n";
    }
    const double step_hx = gdml_world_box ? gdml_world_box->GetXHalfLength() : 25.0;
    const double step_hy = gdml_world_box ? gdml_world_box->GetYHalfLength() : 15.0;
    const double step_hz = gdml_world_box ? gdml_world_box->GetZHalfLength() : 15.0;

    // Create a world LV for imprinting the STEP assembly (no placement needed;
    // we extract daughter solids + transforms directly after MakeImprint).
    auto* step_world_box = new G4Box("AssemblyStepWorld_" + fixture_id, step_hx, step_hy, step_hz);
    G4Material* air      = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
    auto* step_world_lv =
        new G4LogicalVolume(step_world_box, air, "AssemblyStepWorldLV_" + fixture_id);
    G4ThreeVector step_pos;
    G4RotationMatrix step_rot;
    G4OCCTAssemblyVolume* step_assembly =
        G4OCCTAssemblyVolume::FromSTEP(step_path.string(), mat_map);
    step_assembly->MakeImprint(step_world_lv, step_pos, &step_rot);
    const std::vector<ComponentSpec> step_components = ExtractComponents(step_world_lv);

    // Count material mismatches between GDML and STEP components (one-time check).
    const std::size_t material_mismatches =
        CountMaterialMismatches(gdml_components, step_components);

    // Compute the world-frame centre of each GDML component solid: the centre
    // of the local bounding-box AABB transformed by the component's placement.
    // These origins are used as the shared launch points for both GDML and STEP
    // traces so that per-ray crossings remain comparable between the two
    // representations.
    auto ComponentCenter = [](const ComponentSpec& comp) -> G4ThreeVector {
      G4ThreeVector bbox_min;
      G4ThreeVector bbox_max;
      comp.solid->BoundingLimits(bbox_min, bbox_max);
      const G4ThreeVector local_center = 0.5 * (bbox_min + bbox_max);
      return comp.translation + comp.rotation * local_center;
    };

    std::vector<G4ThreeVector> gdml_centers;
    gdml_centers.reserve(gdml_components.size());
    for (const auto& comp : gdml_components) {
      gdml_centers.push_back(ComponentCenter(comp));
    }

    const std::vector<G4ThreeVector> directions = GenerateDirections(ray_count);

    // Total rays: ray_count directions from each component's centre.
    const std::size_t n_components = std::min(gdml_components.size(), step_components.size());
    const std::size_t total_rays   = n_components * directions.size();

    // Tolerance: Geant4 surface tolerance (canonical geometry comparison threshold).
    const G4double kTolerance = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();

    for (auto _ : state) {
      // ── GDML timing ──────────────────────────────────────────────────────
      std::vector<std::vector<Crossing>> gdml_crossings_per_ray(total_rays);
      const auto gdml_begin = std::chrono::steady_clock::now();
      for (std::size_t c = 0; c < n_components; ++c) {
        for (std::size_t i = 0; i < directions.size(); ++i) {
          gdml_crossings_per_ray[c * directions.size() + i] =
              TraceRay(gdml_components, gdml_centers[c], directions[i]);
        }
      }
      const auto gdml_end = std::chrono::steady_clock::now();
      const double gdml_ms =
          std::chrono::duration<double, std::milli>(gdml_end - gdml_begin).count();

      // ── STEP timing ───────────────────────────────────────────────────────
      std::vector<std::vector<Crossing>> step_crossings_per_ray(total_rays);
      const auto step_begin = std::chrono::steady_clock::now();
      for (std::size_t c = 0; c < n_components; ++c) {
        for (std::size_t i = 0; i < directions.size(); ++i) {
          // Use the same world-space ray origins as GDML to ensure
          // per-ray comparability between the two geometries.
          step_crossings_per_ray[c * directions.size() + i] =
              TraceRay(step_components, gdml_centers[c], directions[i]);
        }
      }
      const auto step_end = std::chrono::steady_clock::now();
      const double step_ms =
          std::chrono::duration<double, std::milli>(step_end - step_begin).count();

      // ── Compare ───────────────────────────────────────────────────────────
      std::size_t mismatches        = 0U;
      std::size_t gdml_crossing_cnt = 0U;
      std::size_t step_crossing_cnt = 0U;
      std::vector<G4ThreeVector> gdml_hits;
      std::vector<G4ThreeVector> step_hits;
      gdml_hits.reserve(total_rays * 6U);
      step_hits.reserve(total_rays * 6U);

      for (std::size_t i = 0; i < total_rays; ++i) {
        mismatches +=
            CompareRayCrossings(gdml_crossings_per_ray[i], step_crossings_per_ray[i], kTolerance);
        gdml_crossing_cnt += gdml_crossings_per_ray[i].size();
        step_crossing_cnt += step_crossings_per_ray[i].size();
        for (const auto& c : gdml_crossings_per_ray[i]) {
          gdml_hits.push_back(c.point);
        }
        for (const auto& c : step_crossings_per_ray[i]) {
          step_hits.push_back(c.point);
        }
      }

      state.SetIterationTime(step_ms / 1000.0);
      state.counters["gdml_ms"]             = gdml_ms;
      state.counters["step_ms"]             = step_ms;
      state.counters["ray_count"]           = static_cast<double>(total_rays);
      state.counters["mismatches"]          = static_cast<double>(mismatches);
      state.counters["material_mismatches"] = static_cast<double>(material_mismatches);
      state.counters["gdml_crossings"]      = static_cast<double>(gdml_crossing_cnt);
      state.counters["step_crossings"]      = static_cast<double>(step_crossing_cnt);

      if (g_state != nullptr) {
        std::lock_guard<std::mutex> lk(g_state->mu);
        BenchmarkResult res;
        res.fixture_id          = fixture_id;
        res.gdml_ms             = gdml_ms;
        res.step_ms             = step_ms;
        res.ray_count           = total_rays;
        res.mismatches          = mismatches;
        res.material_mismatches = material_mismatches;
        res.gdml_crossings      = gdml_crossing_cnt;
        res.step_crossings      = step_crossing_cnt;
        g_state->results.push_back(res);
      }

      // ── Point cloud output ────────────────────────────────────────────────
      if (!point_cloud_dir.empty()) {
        try {
          const std::filesystem::path out_file =
              point_cloud_dir / (std::string("BM_assembly_rays_") + fixture_id + ".json.gz");
          WritePointCloud(out_file, "assembly-comparison/" + fixture_id, total_rays, gdml_hits,
                          step_hits);
        } catch (const std::exception& err) {
          std::cerr << "Warning: could not write point cloud: " << err.what() << '\n';
        }
      }
    }
  }

  // ─── Summary printing ─────────────────────────────────────────────────────────

  void PrintSummary(std::ostream& out, const SharedState& state) {
    out << "\n=== Assembly GDML vs STEP comparison ===\n\n";
    out << std::left << std::setw(40) << "Fixture" << "  " << std::right << std::setw(8)
        << "GDML ms"
        << "  " << std::setw(8) << "STEP ms" << "  " << std::setw(6) << "Ratio" << "  "
        << std::setw(10) << "Pos.mm." << "  " << std::setw(8) << "Mat.mm." << "  " << std::setw(12)
        << "GDML cross." << "  " << std::setw(12) << "STEP cross." << '\n';
    out << std::string(115U, '-') << '\n';

    for (const auto& r : state.results) {
      const double ratio = (r.gdml_ms > 0.0) ? (r.step_ms / r.gdml_ms) : 0.0;
      out << std::left << std::setw(40) << r.fixture_id << "  " << std::right << std::fixed
          << std::setprecision(2) << std::setw(8) << r.gdml_ms << "  " << std::setw(8) << r.step_ms
          << "  " << std::setw(5) << std::setprecision(1) << ratio << "x  " << std::setw(10)
          << r.mismatches << "  " << std::setw(8) << r.material_mismatches << "  " << std::setw(12)
          << r.gdml_crossings << "  " << std::setw(12) << r.step_crossings << '\n';
    }
    out << '\n';
  }

} // namespace
} // namespace g4occt::benchmarks::assembly

// ─── Entry point (called from main) ──────────────────────────────────────────
// Must be outside the anonymous namespace so it is accessible as
// g4occt::benchmarks::assembly::RunBenchmark from main().

namespace g4occt::benchmarks::assembly {

int RunBenchmark(const std::filesystem::path& fixture_root, std::size_t ray_count,
                 const std::filesystem::path& point_cloud_dir, bool json_to_stdout) {
  // ── Fixture discovery ───────────────────────────────────────────────────────
  // If fixture_root itself contains geometry.gdml + shape.step, treat it as a
  // single-fixture root (per-process isolation mode).  CI invokes the binary
  // once per fixture in this mode so each run gets a clean Geant4 global state
  // and no cross-fixture name conflicts can occur.  Otherwise scan
  // subdirectories for fixture directories as before.
  SharedState shared_state;
  g_state = &shared_state;

  // Helper lambda: register one fixture directory as a Google Benchmark.
  auto RegisterFixture = [ray_count, &point_cloud_dir](const std::filesystem::path& fixture_dir) {
    const std::filesystem::path gdml_path = fixture_dir / "geometry.gdml";
    const std::filesystem::path step_path = fixture_dir / "shape.step";
    const auto normalized                 = fixture_dir.lexically_normal();
    auto fixture_name                     = normalized.filename();
    if (fixture_name.empty()) {
      fixture_name = normalized.parent_path().filename();
    }
    const std::string fixture_id = fixture_name.string();
    benchmark::RegisterBenchmark(
        ("BM_assembly_rays/assembly-comparison/" + fixture_id).c_str(),
        [fixture_id, gdml_path, step_path, ray_count, point_cloud_dir](benchmark::State& st) {
          RunAssemblyBenchmark(st, fixture_id, gdml_path, step_path, ray_count, point_cloud_dir);
        })
        ->UseManualTime()
        ->Iterations(1)
        ->Unit(benchmark::kMillisecond);
  };

  const std::filesystem::path root_gdml = fixture_root / "geometry.gdml";
  const std::filesystem::path root_step = fixture_root / "shape.step";
  const bool root_has_gdml              = std::filesystem::exists(root_gdml);
  const bool root_has_step              = std::filesystem::exists(root_step);
  if (root_has_gdml && root_has_step) {
    // Single-fixture mode: fixture_root IS the fixture directory.
    RegisterFixture(fixture_root);
  } else if (root_has_gdml != root_has_step) {
    // Incomplete single-fixture root: one of geometry.gdml / shape.step is missing.
    std::cerr << "Error: fixture root \"" << fixture_root.string()
              << "\" is incomplete; expected both geometry.gdml and shape.step.\n";
    g_state = nullptr;
    return EXIT_FAILURE;
  } else {
    // Multi-fixture mode: scan subdirectories of fixture_root.
    for (const auto& entry : std::filesystem::directory_iterator(fixture_root)) {
      if (!entry.is_directory()) {
        continue;
      }
      const std::filesystem::path gdml_path = entry.path() / "geometry.gdml";
      const std::filesystem::path step_path = entry.path() / "shape.step";
      if (!std::filesystem::exists(gdml_path)) {
        continue;
      }
      if (!std::filesystem::exists(step_path)) {
        std::cerr << "Info: STEP file not found for fixture " << entry.path().filename().string()
                  << "; run src/tests/fixtures/assembly-comparison/regenerate.sh to regenerate "
                     "fixtures.\n";
        continue;
      }
      RegisterFixture(entry.path());
    }
  }

  benchmark::AddCustomContext("assembly_ray_count", std::to_string(ray_count));
  benchmark::AddCustomContext("assembly_fixture_root", fixture_root.string());

  // Gate callgrind instrumentation around the benchmark hot loop so that
  // fixture preloading (GDML parse, STEP import, geometry construction) is
  // excluded from the profile.  When running outside valgrind, or built
  // without valgrind headers, all CALLGRIND_* macros expand to no-ops.
  CALLGRIND_START_INSTRUMENTATION;
  CALLGRIND_TOGGLE_COLLECT;
  benchmark::RunSpecifiedBenchmarks();
  CALLGRIND_TOGGLE_COLLECT;
  CALLGRIND_STOP_INSTRUMENTATION;
  benchmark::Shutdown();

  std::ostream& report_out = json_to_stdout ? std::cerr : std::cout;
  PrintSummary(report_out, shared_state);

  g_state = nullptr;
  return EXIT_SUCCESS;
}

} // namespace g4occt::benchmarks::assembly

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
  try {
    // Detect JSON-to-stdout mode before benchmark::Initialize strips flags.
    bool has_json_format = false;
    bool has_json_out    = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg(argv[i]);
      if (arg == "--benchmark_format=json") {
        has_json_format = true;
      }
      if (arg.starts_with("--benchmark_out=") || arg == "--benchmark_out") {
        has_json_out = true;
      }
    }
    const bool json_to_stdout = has_json_format && !has_json_out;

    benchmark::Initialize(&argc, argv);

    // Parse positional arguments (after benchmark flags are removed).
    std::size_t ray_count = 2048U;
    if (argc > 1) {
      ray_count = static_cast<std::size_t>(std::stoul(argv[1]));
    }

    const std::filesystem::path fixture_root =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::benchmarks::assembly::DefaultAssemblyFixtureRoot();

    const std::filesystem::path point_cloud_dir =
        argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{};

    return g4occt::benchmarks::assembly::RunBenchmark(fixture_root, ray_count, point_cloud_dir,
                                                      json_to_stdout);

  } catch (const std::exception& error) {
    std::cerr << "FAIL: bench_assembly_navigator threw: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
