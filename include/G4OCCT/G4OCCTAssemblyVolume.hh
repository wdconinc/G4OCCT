// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTAssemblyVolume.hh
/// @brief Declaration of G4OCCTAssemblyVolume.

#ifndef G4OCCT_G4OCCTAssemblyVolume_hh
#define G4OCCT_G4OCCTAssemblyVolume_hh

#include "G4OCCT/G4OCCTLogicalVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <G4AssemblyVolume.hh>
#include <G4String.hh>

// OCCT (forward-declared where possible; full includes in .cc)
#include <TDF_Label.hxx>
#include <gp_Trsf.hxx>

#include <map>
#include <string>

/**
 * @brief Extends Geant4's G4AssemblyVolume with an OCCT XDE label reference.
 *
 * Imports a STEP assembly file using the OCCT Extended Data Exchange (XDE)
 * layer and constructs the corresponding Geant4 volume hierarchy.
 *
 * ## OCCT XDE to Geant4 mapping
 *
 * | XDE entity                          | Geant4 result                       |
 * |-------------------------------------|-------------------------------------|
 * | Top-level assembly label            | `G4OCCTAssemblyVolume` (this class) |
 * | Simple-shape label (first use)      | `G4OCCTSolid` + `G4OCCTLogicalVolume` |
 * | Simple-shape label (repeated use)   | Shared `G4OCCTLogicalVolume`        |
 * | Reference label + TopLoc_Location   | `AddPlacedVolume()` on parent assembly |
 * | XDE name attribute                  | Volume name string                  |
 * | XDE material attribute (if present) | `G4Material*` via `G4OCCTMaterialMap` |
 * | Part name (fallback when no mat attr) | `G4Material*` via `G4OCCTMaterialMap` |
 *
 * ## Instance sharing
 *
 * When the same XDE shape label is referenced from multiple locations the
 * corresponding `G4OCCTLogicalVolume` is created only once and reused for
 * each placement (incrementing the copy number each time).  This mirrors
 * Geant4's own convention.
 *
 * ## Thread safety
 *
 * This class constructs the Geant4 geometry during `FromSTEP()` and is
 * subsequently read-only.  It inherits `G4AssemblyVolume`'s thread-safety
 * guarantees after construction.
 *
 * ## Usage
 * ```cpp
 * G4OCCTMaterialMap matMap;
 * matMap.Add("Al 6061-T6", G4NistManager::Instance()->FindOrBuildMaterial("G4_Al"));
 *
 * auto* assembly = G4OCCTAssemblyVolume::FromSTEP("detector.step", matMap);
 *
 * // Imprint into the world logical volume
 * G4ThreeVector pos;
 * G4RotationMatrix rot;
 * assembly->MakeImprint(worldLV, pos, &rot);
 * ```
 *
 * See docs/step_assembly_import.md for the full design and background.
 */
class G4OCCTAssemblyVolume : public G4AssemblyVolume {
public:
  G4OCCTAssemblyVolume()  = default;
  ~G4OCCTAssemblyVolume() = default;

  /**
   * Import a STEP assembly file and construct the Geant4 volume hierarchy.
   *
   * Reads the STEP file at @p path using `STEPCAFControl_Reader` (the OCCT
   * XDE reader that preserves assembly structure, names, and material
   * attributes).  The label tree is traversed depth-first; each simple-shape
   * leaf creates a `G4OCCTSolid` + `G4OCCTLogicalVolume`, and each placement
   * is recorded via `AddPlacedVolume()` on the top-level assembly.
   *
   * Every material name encountered in the STEP file must be present in
   * @p materialMap; an unresolved name triggers a fatal `G4Exception`.
   * When a shape carries no XDE material attribute, its part (label) name
   * is used as the lookup key instead, accommodating STEP writers that do
   * not write material attributes.
   *
   * The leaf shapes are recentered (bounding-box centroid moved to the OCCT
   * origin) before being wrapped in `G4OCCTSolid`; the recentering offset is
   * absorbed into the placement transformation so that parts appear at their
   * correct world positions.
   *
   * @param path        Filesystem path to the STEP file.
   * @param materialMap Map from STEP material name or part name to `G4Material*`.
   * @return Pointer to a newly heap-allocated `G4OCCTAssemblyVolume` owned by
   *         the caller.  The returned object already contains all child
   *         volumes; call `MakeImprint()` to place it in the world.
   * @throws std::runtime_error if the file cannot be read or yields no shapes.
   */
  static G4OCCTAssemblyVolume* FromSTEP(const std::string& path,
                                        const G4OCCTMaterialMap& materialMap);

  /**
   * Return all logical volumes created during import, keyed by part name.
   *
   * The map is populated during `FromSTEP()`.  When two STEP parts share
   * the same name the later name is suffixed with `_<n>` so that every entry
   * is unique.
   */
  const std::map<G4String, G4OCCTLogicalVolume*>& GetLogicalVolumes() const {
    return fLogicalVolumes;
  }

private:
  /// All G4OCCTLogicalVolume objects created during import, keyed by unique name.
  std::map<G4String, G4OCCTLogicalVolume*> fLogicalVolumes;

  /// Build context passed through the recursive traversal.
  struct BuildContext;

  /**
   * Recursively import an XDE label into the Geant4 hierarchy.
   *
   * @param label         XDE label to import (assembly, simple shape, or reference).
   * @param parentAssembly Non-null assembly to add child volumes to; may be
   *                       @c this (the top-level) or an intermediate
   *                       `G4AssemblyVolume`.
   * @param composedTrsf  Accumulated rigid-body transformation from the root
   *                       to this label's coordinate frame.
   * @param ctx           Mutable build context (prototype map, copy counter, …).
   */
  static void ImportLabel(const TDF_Label& label, G4AssemblyVolume* parentAssembly,
                          const gp_Trsf& composedTrsf, BuildContext& ctx);
};

#endif // G4OCCT_G4OCCTAssemblyVolume_hh
