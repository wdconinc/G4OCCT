// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTMaterialMapReader.hh
/// @brief Declaration of G4OCCTMaterialMapReader.

#ifndef G4OCCT_G4OCCTMaterialMapReader_hh
#define G4OCCT_G4OCCTMaterialMapReader_hh

#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <G4GDMLReadStructure.hh>
#include <G4String.hh>

/**
 * @brief Parses an XML material-map file into a G4OCCTMaterialMap.
 *
 * Subclasses `G4GDMLReadStructure` to gain zero-duplication access to all
 * `G4GDMLReadMaterials` protected parsing methods (`MaterialRead`,
 * `ElementRead`, `IsotopeRead`, `FractionRead`, `DRead`, …).  Only the
 * dispatching logic for the custom G4OCCT attributes (`stepName` and
 * `geant4Name`) is new code.
 *
 * ## File format
 *
 * The root element must be `<materials>`.  Two entry types are supported:
 *
 * **Type 1 — NIST alias** (no `<fraction>` children required):
 * ```xml
 * <material stepName="AISI 316L" geant4Name="G4_STAINLESS-STEEL"/>
 * ```
 * `geant4Name` is passed directly to
 * `G4NistManager::FindOrBuildMaterial()`.  An unrecognised name is a fatal
 * error.
 *
 * **Type 2 — Inline GDML material definition**:
 * ```xml
 * <element name="Si" Z="14"><atom value="28.085"/></element>
 * <material stepName="FR4" name="FR4" state="solid">
 *   <D value="1.86" unit="g/cm3"/>
 *   <fraction n="0.18" ref="Si"/>
 *   <fraction n="0.39" ref="O"/>
 *   ...
 * </material>
 * ```
 * The `name` attribute is the GDML registry key; it must be present for
 * inline definitions.  Setting `name` equal to `stepName` is recommended
 * for clarity.
 *
 * `<element>` and `<isotope>` definitions must appear before `<material>`
 * entries that reference them.
 *
 * ## Usage
 * ```cpp
 * G4OCCTMaterialMapReader reader;
 * G4OCCTMaterialMap map = reader.ReadFile("materials.xml");
 * auto* assembly = G4OCCTAssemblyVolume::FromSTEP("detector.step", map);
 * ```
 */
class G4OCCTMaterialMapReader : public G4GDMLReadStructure {
public:
  G4OCCTMaterialMapReader()  = default;
  ~G4OCCTMaterialMapReader() override = default;

  /**
   * Parse the material-map XML file at @p path and return the populated map.
   *
   * @param path Filesystem path to the XML material-map file.
   * @return `G4OCCTMaterialMap` with one entry per `<material>` element.
   * @throws G4Exception (FatalException) on any parse or resolution error.
   */
  G4OCCTMaterialMap ReadFile(const G4String& path);
};

#endif // G4OCCT_G4OCCTMaterialMapReader_hh
