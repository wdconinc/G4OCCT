// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTMaterialMap.hh
/// @brief Declaration of G4OCCTMaterialMap.

#ifndef G4OCCT_G4OCCTMaterialMap_hh
#define G4OCCT_G4OCCTMaterialMap_hh

#include <G4String.hh>

#include <cstddef>
#include <map>

class G4Material;

/**
 * @brief Maps STEP material names to Geant4 G4Material objects.
 *
 * Provides a strict, no-fallback lookup table from the material name strings
 * found in a STEP file (via the XDE material attribute) to the corresponding
 * Geant4 `G4Material` pointers.  The design follows the material-bridging
 * policy described in docs/material_bridging.md: every name encountered during
 * STEP import must have a registered entry; unresolved names produce a fatal
 * `G4Exception`.
 *
 * ### Usage
 * ```cpp
 * G4OCCTMaterialMap matMap;
 * matMap.Add("Al 6061-T6", G4NistManager::Instance()->FindOrBuildMaterial("G4_Al"));
 * matMap.Add("FR4",        myFR4Material);
 *
 * G4Material* mat        = matMap.Resolve("Al 6061-T6");  // returns the G4_Al pointer
 * G4Material* unknownMat = matMap.Resolve("Unknown");     // throws fatal G4Exception
 * ```
 */
class G4OCCTMaterialMap {
public:
  G4OCCTMaterialMap() = default;

  /**
   * Register a mapping from a STEP material name to a Geant4 material.
   *
   * If @p stepName is already registered the previous entry is silently
   * overwritten.
   *
   * @param stepName STEP material name string (case-sensitive).
   * @param material Non-null pointer to a Geant4 material.
   */
  void Add(const G4String& stepName, G4Material* material);

  /**
   * Look up the Geant4 material for a given STEP material name.
   *
   * @param stepName STEP material name to resolve.
   * @return Non-null pointer to the registered G4Material.
   * @throws G4Exception (severity FatalException) if @p stepName is not
   *         registered.
   */
  G4Material* Resolve(const G4String& stepName) const;

  /**
   * Return true if @p stepName has been registered.
   */
  bool Contains(const G4String& stepName) const;

  /**
   * Return the number of registered entries.
   */
  std::size_t Size() const { return fMap.size(); }

private:
  std::map<G4String, G4Material*> fMap;
};

#endif // G4OCCT_G4OCCTMaterialMap_hh
