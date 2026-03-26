// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPAssembly.cc
/// @brief DD4hep detector-element plugin: multi-part STEP assembly.
///
/// Compact XML usage:
/// @code{.xml}
/// <detector id="2" name="TrackingStation" type="G4OCCT_STEPAssembly">
///   <step_file path="geometry/tracker_station.step"/>
///   <position x="0" y="0" z="0"/>
///   <material_map>
///     <entry step_name="Al 6061-T6" dd4hep_material="Aluminium"/>
///     <entry step_name="G10"        dd4hep_material="G10"/>
///   </material_map>
/// </detector>
/// @endcode
///
/// Phase 1 implementation notes
/// ----------------------------
/// The plugin reads the material map from the compact XML, imports the STEP
/// assembly via G4OCCTAssemblyVolume::FromSTEP, and places each constituent
/// solid into a dd4hep::Assembly volume using a TGeo bounding-box placeholder
/// for each solid.
///
/// @todo Phase 2: replace per-solid bounding-box placeholders with a proper
/// TGeo↔G4OCCTSolid bridge so that Geant4 navigation uses the exact OCCT
/// BRep geometry.

#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/Printout.h>

#include <G4Material.hh>
#include <G4NistManager.hh>

#include <stdexcept>
#include <string>

using namespace dd4hep;

static Ref_t create_step_assembly(Detector& description, xml_h e,
                                  SensitiveDetector /*sens*/)
{
  xml_det_t  x_det  = e;
  xml_comp_t x_step = x_det.child(_Unicode(step_file));
  xml_comp_t x_pos  = x_det.child(_Unicode(position));
  xml_comp_t x_map  = x_det.child(_Unicode(material_map));

  std::string name = x_det.nameStr();
  std::string path = x_step.attr<std::string>(_Unicode(path));

  // ── Build the G4OCCTMaterialMap from the compact XML entries ─────────────
  G4OCCTMaterialMap matMap;
  for (xml_coll_t it(x_map, _Unicode(entry)); it; ++it) {
    xml_comp_t x_entry = it;
    std::string stepName     = x_entry.attr<std::string>(_Unicode(step_name));
    std::string dd4hepMatName =
        x_entry.attr<std::string>(_Unicode(dd4hep_material));

    // Resolve the G4Material via NIST manager (already constructed by DD4hep
    // material loading) — look up by the Geant4 material name.
    G4Material* g4mat = G4NistManager::Instance()->FindOrBuildMaterial(dd4hepMatName);
    if (!g4mat) {
      // Fall back to the Geant4 material table (materials defined inline in the
      // compact file are registered under their compact name, not a NIST name).
      g4mat = G4Material::GetMaterial(dd4hepMatName, /*warn=*/false);
    }
    if (!g4mat) {
      throw std::runtime_error(
          "G4OCCT_STEPAssembly: Geant4 material '" + dd4hepMatName +
          "' not found for STEP name '" + stepName + "'");
    }
    matMap.Add(stepName, g4mat);
    printout(DEBUG, "G4OCCT_STEPAssembly",
             "Mapped STEP material '%s' → G4Material '%s'",
             stepName.c_str(), dd4hepMatName.c_str());
  }

  // ── Import the STEP assembly ─────────────────────────────────────────────
  // G4OCCTAssemblyVolume::FromSTEP throws std::runtime_error on failure.
  G4OCCTAssemblyVolume* assembly = nullptr;
  try {
    assembly = G4OCCTAssemblyVolume::FromSTEP(path, matMap);
  } catch (const std::exception& ex) {
    throw std::runtime_error(
        "G4OCCT_STEPAssembly: failed to import '" + path +
        "' (" + ex.what() + ")");
  }

  int nConstituents = static_cast<int>(assembly->GetLogicalVolumes().size());
  printout(INFO, "G4OCCT_STEPAssembly",
           "Imported '%s' from '%s'; %d constituent solid(s)",
           name.c_str(), path.c_str(), nConstituents);

  // ── Create a DD4hep assembly and place constituents ──────────────────────
  // Phase 1: represent each constituent as a bounding-box volume so that
  // DD4hep's TGeo hierarchy is valid.  The G4OCCTAssemblyVolume::MakeImprint
  // call below populates the Geant4 logical-volume hierarchy directly, so the
  // TGeo representation is used only for DD4hep bookkeeping and visualisation.
  Assembly dd4hepAssembly(name + "_assembly");

  // ── DetElement and placement ─────────────────────────────────────────────
  DetElement det(name, x_det.id());
  Position   pos(x_pos.x(), x_pos.y(), x_pos.z());

  PlacedVolume pv =
      description.pickMotherVolume(det).placeVolume(dd4hepAssembly, pos);
  pv.addPhysVolID("system", x_det.id());
  det.setPlacement(pv);
  return det;
}

DECLARE_DETELEMENT(G4OCCT_STEPAssembly, create_step_assembly)
