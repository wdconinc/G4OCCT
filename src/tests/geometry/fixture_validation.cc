// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_validation.hh"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopoDS_Shape.hxx>

#include <cmath>
#include <set>
#include <utility>

namespace {

/// Return true when \p path ends with every component of \p suffix.
bool PathEndsWith(const std::filesystem::path& path, const std::filesystem::path& suffix) {
  auto path_it   = path.end();
  auto suffix_it = suffix.end();
  while (suffix_it != suffix.begin()) {
    if (path_it == path.begin()) {
      return false;
    }
    --suffix_it;
    --path_it;
    if (*path_it != *suffix_it) {
      return false;
    }
  }
  return true;
}

} // namespace

namespace g4occt::tests::geometry {

void ValidationReport::AddInfo(std::string code, std::string text, std::filesystem::path path) {
  AddMessage(ValidationSeverity::kInfo, std::move(code), std::move(text), std::move(path));
}

void ValidationReport::AddWarning(std::string code, std::string text, std::filesystem::path path) {
  AddMessage(ValidationSeverity::kWarning, std::move(code), std::move(text), std::move(path));
}

void ValidationReport::AddError(std::string code, std::string text, std::filesystem::path path) {
  AddMessage(ValidationSeverity::kError, std::move(code), std::move(text), std::move(path));
}

void ValidationReport::Append(const ValidationReport& other) {
  messages_.insert(messages_.end(), other.messages_.begin(), other.messages_.end());
}

bool ValidationReport::Ok() const {
  for (const auto& message : messages_) {
    if (message.severity == ValidationSeverity::kError) {
      return false;
    }
  }
  return true;
}

const std::vector<ValidationMessage>& ValidationReport::Messages() const { return messages_; }

/// Error codes that represent non-equivalence between native and imported geometry.
/// These are the only codes that may be demoted to warnings when a fixture is
/// marked as an expected failure; structural and IO errors are always kept as errors.
static const std::set<std::string> kNonEquivalenceCodes = {
    "fixture.volume_mismatch",           "fixture.ray_origin_state_mismatch",
    "fixture.ray_intersection_mismatch", "fixture.ray_distance_mismatch",
    "fixture.ray_normal_mismatch",
};

ValidationReport ReclassifyExpectedFailures(const ValidationReport& report,
                                            const std::string& reason) {
  ValidationReport rewritten;
  for (const auto& message : report.Messages()) {
    if (message.severity == ValidationSeverity::kError &&
        kNonEquivalenceCodes.count(message.code) != 0U) {
      rewritten.AddWarning("xfail." + message.code, message.text + " (xfail: " + reason + ")",
                           message.path);
      continue;
    }

    if (message.severity == ValidationSeverity::kWarning) {
      rewritten.AddWarning(message.code, message.text, message.path);
      continue;
    }

    if (message.severity == ValidationSeverity::kError) {
      rewritten.AddError(message.code, message.text, message.path);
      continue;
    }

    rewritten.AddInfo(message.code, message.text, message.path);
  }
  return rewritten;
}

FixtureExpectedFailure ExpectedFailureForFixture(const FixtureValidationRequest& request) {
  const std::string& geant4_class = request.fixture.geant4_class;

  if (geant4_class == "G4TwistedBox" || geant4_class == "G4TwistedTrd" ||
      geant4_class == "G4TwistedTrap" || geant4_class == "G4TwistedTubs" ||
      geant4_class == "G4VTwistedFaceted") {
    return {true,
            "fixture STEP files are built from the old ruled-loft generator and must be "
            "regenerated with the updated B-spline loft construction before validation passes"};
  }

  if (geant4_class == "G4Hype" || geant4_class == "G4Paraboloid") {
    return {true, "fixture STEP uses a faceted profile-loft approximation instead of the analytic "
                  "Geant4 surface"};
  }

  if (geant4_class == "G4Ellipsoid" || geant4_class == "G4EllipticalCone" ||
      geant4_class == "G4EllipticalTube" || geant4_class == "G4Polycone" ||
      geant4_class == "G4Polyhedra" || geant4_class == "G4ScaledSolid") {
    return {true,
            "strict native-to-STEP ray-frame alignment for this fixture is not implemented yet"};
  }

  if (geant4_class == "G4CutTubs") {
    return {true, "strict native-to-STEP ray-frame alignment for the tilted cut-tubs fixture is "
                  "not implemented yet"};
  }

  return {};
}

void ValidationReport::AddMessage(const ValidationSeverity severity, std::string code,
                                  std::string text, std::filesystem::path path) {
  messages_.push_back(ValidationMessage{
      severity,
      std::move(code),
      std::move(text),
      std::move(path),
  });
}

std::string ToString(const ValidationSeverity severity) {
  switch (severity) {
  case ValidationSeverity::kInfo:
    return "info";
  case ValidationSeverity::kWarning:
    return "warning";
  case ValidationSeverity::kError:
    return "error";
  }

  return "unknown";
}

ValidationReport ValidateRepositoryLayout(const FixtureRepositoryManifest& manifest) {
  ValidationReport report;

  if (manifest.schema_version.empty()) {
    report.AddError("repository.schema_version",
                    "Repository manifest schema_version must not be empty", manifest.source_path);
  }
  if (manifest.fixture_root.empty()) {
    report.AddError("repository.fixture_root", "Repository manifest fixture_root must not be empty",
                    manifest.source_path);
  } else {
    const std::filesystem::path on_disk_root = manifest.source_path.parent_path();
    if (!PathEndsWith(on_disk_root, manifest.fixture_root)) {
      report.AddError("repository.fixture_root_mismatch",
                      "Repository manifest fixture_root '" + manifest.fixture_root.string() +
                          "' does not match the manifest's on-disk location '" +
                          on_disk_root.string() + "'",
                      manifest.source_path);
    }
  }
  if (manifest.owner.empty()) {
    report.AddError("repository.owner", "Repository manifest owner must not be empty",
                    manifest.source_path);
  }
  if (manifest.policy.step_file_name.empty()) {
    report.AddError("repository.policy.step_file_name",
                    "Repository manifest must declare step_file_name", manifest.source_path);
  }
  if (manifest.policy.provenance_file_name.empty()) {
    report.AddError("repository.policy.provenance_file_name",
                    "Repository manifest must declare provenance_file_name", manifest.source_path);
  }
  if (manifest.policy.volume_unit.empty()) {
    report.AddError("repository.policy.volume_unit", "Repository manifest must declare volume_unit",
                    manifest.source_path);
  }

  const auto root_directory = ResolveRepositoryFixtureRoot(manifest);
  if (!std::filesystem::exists(root_directory)) {
    report.AddError("repository.root_missing", "Fixture repository root directory does not exist",
                    root_directory);
    return report;
  }

  if (!std::filesystem::is_directory(root_directory)) {
    report.AddError("repository.root_not_directory",
                    "Fixture repository root path is not a directory", root_directory);
    return report;
  }

  if (manifest.families.empty()) {
    report.AddWarning("repository.families_empty",
                      "Repository manifest does not list any fixture families",
                      manifest.source_path);
  }

  for (const auto& family : manifest.families) {
    const auto family_directory     = root_directory / family;
    const auto family_manifest_path = ResolveFamilyManifestPath(manifest, family);
    if (!std::filesystem::exists(family_directory)) {
      report.AddError("repository.family_missing",
                      "Fixture family directory does not exist: " + family, family_directory);
      continue;
    }
    if (!std::filesystem::is_directory(family_directory)) {
      report.AddError("repository.family_not_directory",
                      "Fixture family path is not a directory: " + family, family_directory);
      continue;
    }
    if (!std::filesystem::exists(family_manifest_path)) {
      report.AddError("repository.family_manifest_missing",
                      "Fixture family manifest is missing: " + family, family_manifest_path);
    }
  }

  return report;
}

ValidationReport ValidateManifestStructure(const FixtureManifest& manifest) {
  ValidationReport report;

  if (manifest.family.empty()) {
    report.AddError("manifest.family", "Manifest family must not be empty", manifest.source_path);
  }

  if (manifest.schema_version.empty()) {
    report.AddError("manifest.schema_version", "Manifest schema_version must not be empty",
                    manifest.source_path);
  }

  std::set<std::string> fixture_ids;
  for (const auto& fixture : manifest.fixtures) {
    if (fixture.id.empty()) {
      report.AddError("fixture.id", "Fixture id must not be empty", manifest.source_path);
    } else if (!fixture_ids.insert(fixture.id).second) {
      report.AddError("fixture.duplicate_id",
                      "Fixture id is duplicated within the manifest: " + fixture.id,
                      manifest.source_path);
    }

    if (fixture.geant4_class.empty()) {
      report.AddError("fixture.geant4_class", "Fixture geant4_class must not be empty",
                      manifest.source_path);
    }

    if (fixture.relative_directory.empty()) {
      report.AddError("fixture.relative_directory", "Fixture relative_directory must not be empty",
                      manifest.source_path);
    }

    if (fixture.family.empty()) {
      report.AddError("fixture.family", "Fixture family must not be empty", manifest.source_path);
    } else if (fixture.family != manifest.family) {
      report.AddError("fixture.family_mismatch",
                      "Fixture family does not match owning manifest family: " + fixture.family,
                      manifest.source_path);
    }
  }

  return report;
}

ValidationReport ValidateFixtureLayout(const FixtureValidationRequest& request) {
  ValidationReport report;

  if (request.fixture.id.empty()) {
    report.AddError("fixture.id", "Fixture id must not be empty", request.manifest.source_path);
  }
  if (request.fixture.geant4_class.empty()) {
    report.AddError("fixture.geant4_class", "Fixture geant4_class must not be empty",
                    request.manifest.source_path);
  }
  if (request.fixture.relative_directory.empty()) {
    report.AddError("fixture.relative_directory", "Fixture relative_directory must not be empty",
                    request.manifest.source_path);
  }
  if (!request.fixture.family.empty() && request.fixture.family != request.manifest.family) {
    report.AddError("fixture.family_mismatch",
                    "Fixture family does not match owning manifest family: " +
                        request.fixture.family,
                    request.manifest.source_path);
  }
  if (!report.Ok()) {
    return report;
  }

  const auto fixture_directory = ResolveFixtureDirectory(request.manifest, request.fixture);
  if (!std::filesystem::exists(fixture_directory)) {
    report.AddError("fixture.directory_missing", "Fixture directory does not exist",
                    fixture_directory);
    return report;
  }

  if (!std::filesystem::is_directory(fixture_directory)) {
    report.AddError("fixture.directory_not_directory", "Fixture path is not a directory",
                    fixture_directory);
    return report;
  }

  if (request.require_step_file) {
    const auto step_path = ResolveFixtureStepPath(request.manifest, request.fixture);
    if (!std::filesystem::exists(step_path)) {
      if (request.fixture.validation_state == FixtureValidationState::kPlanned) {
        report.AddWarning("fixture.step_pending",
                          "Fixture STEP file not yet present (fixture is planned)", step_path);
      } else {
        report.AddError("fixture.step_missing", "Fixture STEP file is missing", step_path);
      }
    }
  }

  if (request.require_provenance_file) {
    const auto provenance_path = ResolveFixtureProvenancePath(request.manifest, request.fixture);
    if (!std::filesystem::exists(provenance_path)) {
      report.AddError("fixture.provenance_missing", "Fixture provenance file is missing",
                      provenance_path);
    }
  }

  return report;
}

ValidationReport ValidateFixtureGeometry(const FixtureValidationRequest& request,
                                         const FixtureGeometryValidationOptions& options,
                                         FixtureGeometryObservation* observation) {
  ValidationReport report = ValidateFixtureLayout(request);
  if (!report.Ok()) {
    return report;
  }

  const auto step_path = ResolveFixtureStepPath(request.manifest, request.fixture);

  // For planned fixtures whose STEP file has not been fetched yet, skip
  // geometry validation.  The layout check above already emitted a warning.
  // For all other fixtures, a missing STEP file is a hard error.
  if (!std::filesystem::exists(step_path)) {
    if (request.fixture.validation_state != FixtureValidationState::kPlanned) {
      report.AddError("fixture.step_missing", "Fixture STEP file is missing", step_path);
    }
    return report;
  }

  FixtureGeometryObservation local_observation;
  local_observation.step_path = step_path;

  STEPControl_Reader reader;
  const IFSelect_ReturnStatus read_status = reader.ReadFile(step_path.string().c_str());
  if (read_status != IFSelect_RetDone) {
    report.AddError("fixture.step_read_failed", "STEPControl_Reader failed to read STEP file",
                    step_path);
    if (observation != nullptr) {
      *observation = local_observation;
    }
    return report;
  }

  if (reader.TransferRoots() <= 0) {
    report.AddError("fixture.step_transfer_failed",
                    "STEPControl_Reader did not transfer any STEP roots", step_path);
    if (observation != nullptr) {
      *observation = local_observation;
    }
    return report;
  }

  const TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) {
    report.AddError("fixture.shape_null", "Transferred STEP shape is null", step_path);
    if (observation != nullptr) {
      *observation = local_observation;
    }
    return report;
  }

  local_observation.imported = true;

  const BRepCheck_Analyzer analyzer(shape);
  if (!analyzer.IsValid()) {
    report.AddError("fixture.shape_invalid",
                    "BRepCheck_Analyzer reported an invalid imported shape", step_path);
  } else {
    local_observation.topologically_valid = true;
    report.AddInfo("fixture.shape_valid", "BRepCheck_Analyzer accepted the imported shape",
                   step_path);
  }

  GProp_GProps properties;
  BRepGProp::VolumeProperties(shape, properties);
  local_observation.volume_mm3      = properties.Mass();
  local_observation.volume_computed = true;

  if (options.require_positive_volume && !(local_observation.volume_mm3 > 0.0)) {
    report.AddError("fixture.volume_non_positive", "Imported shape volume must be positive",
                    step_path);
  } else {
    report.AddInfo("fixture.volume_computed",
                   "Imported shape volume = " + std::to_string(local_observation.volume_mm3) +
                       " mm^3",
                   step_path);
  }

  if (options.compare_volume_expectations) {
    for (const auto& expectation : request.fixture.expectations) {
      if (expectation.quantity != "volume") {
        continue;
      }
      if (expectation.unit != options.volume_unit) {
        report.AddWarning("fixture.volume_unit_mismatch",
                          "Volume expectation unit does not match policy unit '" +
                              options.volume_unit + "': " + expectation.unit,
                          step_path);
        continue;
      }
      // OCCT BRepGProp::VolumeProperties always returns mm^3.  If the policy
      // unit is not mm3 the comparison would be meaningless, so skip it and
      // emit a clear warning rather than silently comparing in the wrong unit.
      if (options.volume_unit != "mm3") {
        report.AddWarning(
            "fixture.volume_unit_not_mm3",
            "Volume comparison skipped: OCCT reports volumes in mm3 but policy unit is '" +
                options.volume_unit + "'",
            step_path);
        continue;
      }

      const double delta = std::fabs(local_observation.volume_mm3 - expectation.value);
      if (delta > expectation.absolute_tolerance) {
        report.AddError("fixture.volume_mismatch",
                        "Imported shape volume differs from expectation by " +
                            std::to_string(delta) + " mm^3",
                        step_path);
      } else {
        report.AddInfo("fixture.volume_match",
                       "Imported shape volume matches the manifest expectation", step_path);
      }
    }
  }

  if (observation != nullptr) {
    *observation = local_observation;
  }
  return report;
}

} // namespace g4occt::tests::geometry
