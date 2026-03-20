// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_MANIFEST_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_MANIFEST_HH

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace g4occt::tests::geometry {

/** Validation status tracked for a geometry fixture entry. */
enum class FixtureValidationState {
  kPlanned,
  kValidated,
  kRejected,
};

/** Expected scalar quantity attached to a geometry fixture. */
struct FixtureExpectation {
  /// Quantity name, for example `volume`.
  std::string quantity;
  /// Expected scalar value expressed in `unit`.
  double value{0.0};
  /// Absolute tolerance for comparisons in the same `unit`.
  double absolute_tolerance{0.0};
  /// Unit label recorded in the manifest.
  std::string unit;
};

/** Repository-owned geometry fixture entry referenced from a manifest. */
struct FixtureReference {
  /// Stable fixture identifier, typically `{class}-{slug}-v{N}`.
  std::string id;
  /// Geant4 class covered by this fixture.
  std::string geant4_class;
  /// Fixture family directory, for example `direct-primitives`.
  std::string family;
  /// Per-class fixture slug without the version suffix.
  std::string fixture_slug;
  /// Directory holding `shape.step` and related fixture assets.
  std::filesystem::path relative_directory;
  /// Relative path to the STEP asset inside `relative_directory`.
  std::filesystem::path step_file{"shape.step"};
  /// Relative path to provenance metadata inside `relative_directory`.
  std::filesystem::path provenance_file{"provenance.yaml"};
  /// Validation state recorded by the owning manifest.
  FixtureValidationState validation_state{FixtureValidationState::kPlanned};
  /// Expected scalar checks, such as volume comparisons.
  std::vector<FixtureExpectation> expectations;
  /// Additional Geant4 classes that intentionally reuse this canonical asset.
  std::vector<std::string> reused_by;
};

/** In-memory representation of a family manifest document. */
struct FixtureManifest {
  /// Manifest file path on disk.
  std::filesystem::path source_path;
  /// Schema version string recorded in YAML.
  std::string schema_version{"geometry-fixture-manifest/v1"};
  /// Family key owned by the manifest.
  std::string family;
  /// Short human-readable description.
  std::string description;
  /// Concrete Geant4 classes covered by this manifest.
  std::vector<std::string> coverage_classes;
  /// Fixture entries owned by the manifest.
  std::vector<FixtureReference> fixtures;
};

/** Shared policy declared by the repository-level geometry fixture manifest. */
struct FixtureRepositoryPolicy {
  /// Default checked-in STEP asset filename.
  std::string step_file_name{"shape.step"};
  /// Default provenance metadata filename.
  std::string provenance_file_name{"provenance.yaml"};
  /// Unit recorded for volume expectations.
  std::string volume_unit{"mm3"};
  /// Allowed validation status tokens.
  std::vector<std::string> validation_status_values;
};

/** Repository-level geometry fixture manifest. */
struct FixtureRepositoryManifest {
  /// Manifest file path on disk.
  std::filesystem::path source_path;
  /// Schema version string recorded in YAML.
  std::string schema_version{"geometry-fixture-manifest/v1"};
  /// Repository-relative fixture root recorded in YAML.
  std::filesystem::path fixture_root;
  /// Owning source subtree.
  std::string owner;
  /// Shared fixture naming and units policy.
  FixtureRepositoryPolicy policy;
  /// Family directories tracked by the repository manifest.
  std::vector<std::string> families;
};

/**
 * Convert a validation state enum to the manifest token used in YAML.
 *
 * @param state Validation state to stringify.
 * @return Lowercase manifest token.
 */
std::string ToString(FixtureValidationState state);

/**
 * Parse a manifest validation state token.
 *
 * @param token Lowercase manifest token.
 * @return Matching validation state.
 */
FixtureValidationState ParseFixtureValidationState(std::string_view token);

/**
 * Create an empty family manifest document with the shared schema version applied.
 *
 * @param source_path Manifest path on disk.
 * @param family Fixture family key.
 * @param description Human-readable summary.
 * @return Initialised manifest document.
 */
FixtureManifest MakeFixtureManifest(const std::filesystem::path& source_path, std::string family,
                                    std::string description);

/**
 * Append a covered class name if it is not already recorded.
 *
 * @param manifest Manifest to update.
 * @param class_name Concrete Geant4 class name.
 */
void AddCoverageClass(FixtureManifest& manifest, std::string class_name);

/**
 * Append a fixture entry to the manifest.
 *
 * @param manifest Manifest to update.
 * @param fixture Fixture entry to store.
 */
void AddFixture(FixtureManifest& manifest, FixtureReference fixture);

/**
 * Resolve the root directory owned by the repository-level manifest.
 *
 * @param manifest Repository-level manifest.
 * @return Absolute fixture root path.
 */
std::filesystem::path ResolveRepositoryFixtureRoot(const FixtureRepositoryManifest& manifest);

/**
 * Resolve the manifest file for a tracked family directory.
 *
 * @param manifest Repository-level manifest.
 * @param family Family directory key.
 * @return Absolute family manifest path.
 */
std::filesystem::path ResolveFamilyManifestPath(const FixtureRepositoryManifest& manifest,
                                                std::string_view family);

/**
 * Resolve the on-disk directory for a fixture entry.
 *
 * @param manifest Manifest owning the fixture.
 * @param fixture Fixture entry within the manifest.
 * @return Absolute fixture directory path.
 */
std::filesystem::path ResolveFixtureDirectory(const FixtureManifest& manifest,
                                              const FixtureReference& fixture);

/**
 * Resolve the on-disk STEP path for a fixture entry.
 *
 * @param manifest Manifest owning the fixture.
 * @param fixture Fixture entry within the manifest.
 * @return Absolute STEP file path.
 */
std::filesystem::path ResolveFixtureStepPath(const FixtureManifest& manifest,
                                             const FixtureReference& fixture);

/**
 * Resolve the on-disk provenance path for a fixture entry.
 *
 * @param manifest Manifest owning the fixture.
 * @param fixture Fixture entry within the manifest.
 * @return Absolute provenance file path.
 */
std::filesystem::path ResolveFixtureProvenancePath(const FixtureManifest& manifest,
                                                   const FixtureReference& fixture);

/**
 * Read the raw text of a manifest file.
 *
 * @param manifest_path Manifest file to read.
 * @return Entire manifest text.
 */
std::string ReadManifestText(const std::filesystem::path& manifest_path);

/**
 * Parse the repository-level geometry fixture manifest.
 *
 * This intentionally supports only the narrow YAML subset used by the checked-in
 * geometry fixture manifests so the test utility remains dependency-light.
 *
 * @param manifest_path Repository-level manifest file.
 * @return Parsed repository manifest.
 */
FixtureRepositoryManifest
ParseFixtureRepositoryManifest(const std::filesystem::path& manifest_path);

/**
 * Parse a family-level geometry fixture manifest.
 *
 * This parser accepts the current scaffold plus future fixture entries that use
 * scalar values, bracket lists, and nested `expectations` lists.
 *
 * @param manifest_path Family manifest file.
 * @return Parsed family manifest.
 */
FixtureManifest ParseFixtureManifestFile(const std::filesystem::path& manifest_path);

} // namespace g4occt::tests::geometry

#endif
