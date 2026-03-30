// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPAssemblySD.hh
/// @brief DD4hep sim action helper for G4OCCT STEP assembly sensitive detectors.
///
/// Provides G4OCCTAssemblySDSetup, a helper called from ConstructSDandField
/// to assign Geant4 SDs to the logical volumes of a registered G4OCCT STEP assembly.

#ifndef G4OCCT_DD4HEP_STEPAssemblySD_hh
#define G4OCCT_DD4HEP_STEPAssemblySD_hh

#include <string>
#include <utility>
#include <vector>

class G4VSensitiveDetector;

/**
 * @brief Helper for assigning SDs to G4OCCT STEP assembly volumes.
 *
 * In the DD4hep workflow, sensitive detectors (as G4VSensitiveDetector*
 * objects) are created during ConstructSDandField().  For G4OCCT STEP
 * assemblies the constituent G4LogicalVolumes bypass the TGeo representation
 * and are therefore not reachable via the normal DD4hep volume-SD association.
 *
 * This class bridges the gap: after creating SD objects, call
 * `G4OCCTAssemblySDSetup::Apply` with the detector name (as registered in the
 * compact XML) to retrieve the assembly from `G4OCCTAssemblyRegistry` and
 * call `ApplySDMap` on it.
 *
 * ### Usage (standalone Geant4)
 * ```cpp
 * void MyDetConstruction::ConstructSDandField() {
 *   auto* absoSD = new CalorimeterSD("AbsorberSD", ...);
 *   G4SDManager::GetSDMpointer()->AddNewDetector(absoSD);
 *
 *   G4OCCTAssemblySDSetup::Apply("Calorimeter",
 *                                {{"Absorber", absoSD}});
 * }
 * ```
 *
 * ### Usage (DD4hep-driven)
 * Build a G4OCCTSensitiveDetectorMap from the DD4hep readout and call Apply
 * from a dd4hep::sim::Geant4SensDetActionSequence or a custom ConstructSDandField.
 */
class G4OCCTAssemblySDSetup {
public:
  /**
   * Retrieve the named assembly from G4OCCTAssemblyRegistry and assign the
   * given SDs to matching logical volumes via ApplySDMap.
   *
   * @param detectorName  Assembly name as registered in G4OCCTAssemblyRegistry
   *                      (equals the DD4hep detector `name` attribute).
   * @param assignments   Vector of (volumePattern, sd*) pairs.
   * @return Number of logical volumes that received an SD assignment.
   *         Returns 0 and emits a JustWarning if the assembly is not found.
   */
  static std::size_t Apply(const std::string& detectorName,
                            const std::vector<std::pair<std::string, G4VSensitiveDetector*>>&
                                assignments);
};

#endif // G4OCCT_DD4HEP_STEPAssemblySD_hh
