// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_SOLID_BUILDER_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_SOLID_BUILDER_HH

#include "geometry/fixture_validation.hh"

#include <yaml-cpp/yaml.h>

#include <TopoDS_Shape.hxx>

#include <G4ThreeVector.hh>
#include <G4VSolid.hh>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace g4occt::tests::geometry {

/** Parsed fixture provenance document together with its on-disk path. */
struct FixtureProvenance {
  /// Absolute path to the provenance YAML file.
  std::filesystem::path source_path;
  /// Root YAML node loaded from the file.
  YAML::Node document;
};

/**
 * Load and parse a fixture provenance YAML file.
 *
 * @param path Absolute path to `provenance.yaml`.
 * @return Parsed provenance object.
 * @throws std::runtime_error if the file cannot be loaded.
 */
FixtureProvenance ParseFixtureProvenance(const std::filesystem::path& path);

/**
 * Return the Geant4 class name recorded in a provenance document.
 *
 * @param provenance Parsed provenance document.
 * @return Class name string, e.g. `"G4Box"`.
 * @throws std::runtime_error if the `shape.geant4_class` key is missing.
 */
std::string Geant4Class(const FixtureProvenance& provenance);

/**
 * Construct the native Geant4 solid described by a fixture provenance.
 *
 * @param provenance Parsed provenance document.
 * @return Heap-allocated Geant4 solid owned by the caller.
 * @throws std::runtime_error if a required YAML key is missing or the class is unsupported.
 */
std::unique_ptr<G4VSolid> BuildNativeSolid(const FixtureProvenance& provenance);

/**
 * Construct the native benchmark solid for a fixture.
 *
 * For most fixtures this delegates to `BuildNativeSolid`.  For `G4OCCTSolid`
 * fixtures, which have no separate native Geant4 class, a second `G4OCCTSolid`
 * instance is created from the same imported STEP file so that benchmark timing
 * and point-cloud generation can run without a native counterpart.
 *
 * @param request    Validation request that specifies the fixture asset paths.
 * @param provenance Parsed provenance document.
 * @return Heap-allocated Geant4 solid owned by the caller.
 * @throws std::runtime_error if the STEP file is missing or a required YAML key is absent.
 */
std::unique_ptr<G4VSolid> BuildNativeSolidForRequest(const FixtureValidationRequest& request,
                                                     const FixtureProvenance& provenance);

/**
 * Load the STEP file for a fixture and return its OCCT shape.
 *
 * @param request Validation request that specifies the fixture asset paths.
 * @return Transferred OCCT shape.
 * @throws std::runtime_error if the STEP file cannot be read or transferred.
 */
TopoDS_Shape LoadImportedShape(const FixtureValidationRequest& request);

/**
 * Compute the centre of the axis-aligned bounding box of a solid.
 *
 * @param solid Solid to query.
 * @return Centre point in the solid's local coordinate system.
 */
G4ThreeVector BoundingBoxCenter(const G4VSolid& solid);

/**
 * Choose the fixture-class-specific comparison origin for ray tests.
 *
 * - `G4OCCTSolid`: the centre of the axis-aligned bounding box.
 * - `G4Tet`: centroid of the four vertices from the fixture provenance.
 * - Ellipsoidal and twisted solids: the coordinate origin `(0, 0, 0)`.
 * - All other solids: the centre of the axis-aligned bounding box.
 *
 * @param provenance Parsed provenance document (used to read the class).
 * @param solid      Corresponding Geant4 solid (used for the bounding-box fallback).
 * @return Comparison origin in the solid's local coordinate frame.
 */
G4ThreeVector FixtureComparisonOrigin(const FixtureProvenance& provenance, const G4VSolid& solid);

/**
 * Generate a deterministic, approximately uniform set of unit direction vectors.
 *
 * The first up to six entries are the positive and negative Cartesian axis
 * directions; the remaining entries follow the Fibonacci / golden-angle spiral
 * on the unit sphere.
 *
 * @param count Total number of direction vectors to produce.
 * @return Vector of unit-length `G4ThreeVector` values.
 */
std::vector<G4ThreeVector> GenerateDirections(std::size_t count);

/**
 * Generate bounding-box sample points using a 3-D Halton low-discrepancy sequence.
 *
 * Points are distributed deterministically and approximately uniformly across
 * the axis-aligned bounding box of @p solid, as defined by its
 * `BoundingLimits()` method.  The Halton sequence uses bases 2, 3, and 5 for
 * the three spatial dimensions.
 *
 * @param solid  Solid whose `BoundingLimits()` defines the sampling volume.
 * @param count  Number of points to generate.
 * @return       Vector of @p count 3-D points distributed across the bounding box.
 */
std::vector<G4ThreeVector> GenerateBoundingBoxPoints(const G4VSolid& solid, std::size_t count);

} // namespace g4occt::tests::geometry

#endif
