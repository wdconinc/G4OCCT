// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_VALIDATION_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_VALIDATION_HH

#include "geometry/fixture_manifest.hh"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace g4occt::tests::geometry {

/** Severity level emitted by geometry fixture validation helpers. */
enum class ValidationSeverity {
  kInfo,
  kWarning,
  kError,
};

/** Structured validation message for manifest or file-layout checks. */
struct ValidationMessage {
  /// Severity level for the message.
  ValidationSeverity severity{ValidationSeverity::kInfo};
  /// Stable machine-readable code.
  std::string code;
  /// Human-readable diagnostic text.
  std::string text;
  /// File or directory associated with the diagnostic.
  std::filesystem::path path;
};

/** Reusable validation result accumulator for geometry fixture helpers. */
class ValidationReport {
public:
  /** Record an informational message. */
  void AddInfo(std::string code, std::string text, std::filesystem::path path = {});

  /** Record a warning message. */
  void AddWarning(std::string code, std::string text, std::filesystem::path path = {});

  /** Record an error message. */
  void AddError(std::string code, std::string text, std::filesystem::path path = {});

  /** Append all messages from another report. */
  void Append(const ValidationReport& other);

  /** @return True when no error-level messages were recorded. */
  bool Ok() const;

  /** @return Immutable list of collected messages. */
  const std::vector<ValidationMessage>& Messages() const;

private:
  void AddMessage(ValidationSeverity severity, std::string code, std::string text,
                  std::filesystem::path path);

  std::vector<ValidationMessage> messages_;
};

/**
 * Known expected-failure policy for a geometry fixture.
 *
 * Two independent flags allow safety mismatches to be suppressed separately
 * from ray / inside / volume mismatches.  A fixture may have either or both
 * flags set.
 *
 * When `enabled` is true ALL non-equivalence error codes are demoted to
 * warnings (existing behaviour).  `safety_enabled` is reserved for future
 * use; the safety non-equivalence allowlist is currently empty because
 * Geant4-vs-OCCT safety distance differences are no longer treated as errors
 * (see `CompareFixtureSafety`).  OCCT lower-bound violations are hard
 * failures that cannot be reclassified by either flag.
 */
struct FixtureExpectedFailure {
  /// Reclassify all non-equivalence errors (ray, inside, volume, safety) as xfail warnings.
  bool enabled{false};
  /// Human-readable reason for the full expected failure.
  std::string reason;

  /// Reclassify only safety scalar distance errors as xfail warnings.
  bool safety_enabled{false};
  /// Human-readable reason for the safety-only expected failure.
  std::string safety_reason;
};

/** File-level validation request for a single fixture entry. */
struct FixtureValidationRequest {
  /// Manifest that owns the fixture entry.
  FixtureManifest manifest;
  /// Fixture entry to validate.
  FixtureReference fixture;
  /// Require `shape.step` to exist.
  bool require_step_file{true};
  /// Require `provenance.yaml` to exist; missing file is reported as an error.
  bool require_provenance_file{false};
};

/** Summary of imported geometry observations for one fixture. */
struct FixtureGeometryObservation {
  /// Imported STEP path.
  std::filesystem::path step_path;
  /// True if the STEP file was successfully transferred into a TopoDS_Shape.
  bool imported{false};
  /// True if BRepCheck_Analyzer reported a valid shape.
  bool topologically_valid{false};
  /// True if a volume was computed.
  bool volume_computed{false};
  /// Computed OCCT volume in mm^3.
  double volume_mm3{0.0};
};

/** Options controlling the OCCT validation path for a fixture. */
struct FixtureGeometryValidationOptions {
  /// Require a positive volume after import.
  bool require_positive_volume{true};
  /// Compare the imported volume against manifest expectations when present.
  bool compare_volume_expectations{true};
  /// Unit to use when comparing volume expectations (must match policy.volume_unit).
  std::string volume_unit{"mm3"};
};

/**
 * Convert a validation severity enum to printable text.
 *
 * @param severity Severity value to stringify.
 * @return Lowercase severity token.
 */
std::string ToString(ValidationSeverity severity);

/**
 * Reclassify non-equivalence error diagnostics as expected-failure warnings.
 *
 * Errors whose codes are in the applicable allowlists are demoted to
 * warnings with an `xfail.` prefix according to the policy in @p failure:
 *
 *  - Non-equivalence (applied when `failure.enabled` is true):
 *    - `fixture.volume_mismatch`
 *    - `fixture.ray_origin_state_mismatch`
 *    - `fixture.ray_intersection_mismatch`
 *    - `fixture.ray_distance_mismatch`
 *    - `fixture.ray_normal_mismatch`
 *    - `fixture.surface_normal_mismatch`
 *    - `fixture.inside_classification_mismatch`
 *  - Safety-only (applied when `failure.safety_enabled` is true): currently empty;
 *    reserved for future safety non-equivalence codes.
 *
 * Structural and IO errors (missing files, STEP read/transfer failures, etc.)
 * are kept as errors regardless of the failure policy.
 *
 * @param report Source report to rewrite.
 * @param failure Expected-failure policy describing which codes to demote and why.
 * @return A copy with allowlisted error severities demoted to warnings and `xfail.` code prefixes.
 */
ValidationReport ReclassifyExpectedFailures(const ValidationReport& report,
                                            const FixtureExpectedFailure& failure);

/**
 * Return the known expected-failure policy for a fixture.
 *
 * @param request Fixture under consideration.
 * @return Enabled policy when the fixture is currently expected to fail strict comparison.
 */
FixtureExpectedFailure ExpectedFailureForFixture(const FixtureValidationRequest& request);

/**
 * Validate the repository-level fixture manifest and family directory layout.
 *
 * @param manifest Repository-level manifest to inspect.
 * @return Validation report covering shared scaffold layout.
 */
ValidationReport ValidateRepositoryLayout(const FixtureRepositoryManifest& manifest);

/**
 * Validate a manifest's bookkeeping before OCCT-specific geometry checks run.
 *
 * @param manifest Manifest to inspect.
 * @return Validation report covering duplicate IDs and missing metadata.
 */
ValidationReport ValidateManifestStructure(const FixtureManifest& manifest);

/**
 * Validate the file layout for a single fixture entry.
 *
 * @param request Request describing the fixture and required assets.
 * @return Validation report covering missing directories and files.
 */
ValidationReport ValidateFixtureLayout(const FixtureValidationRequest& request);

/**
 * Load a fixture STEP file with OCCT and evaluate validity and volume checks.
 *
 * @param request Request describing the fixture to validate.
 * @param options Controls for optional volume checks.
 * @param observation Optional destination for computed geometry facts.
 * @return Validation report covering STEP import, topology validity, and volume.
 */
ValidationReport ValidateFixtureGeometry(const FixtureValidationRequest& request,
                                         const FixtureGeometryValidationOptions& options = {},
                                         FixtureGeometryObservation* observation         = nullptr);

} // namespace g4occt::tests::geometry

#endif
