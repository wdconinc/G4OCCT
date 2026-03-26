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
/// assembly via G4OCCTAssemblyVolume::FromSTEP (in a separate OCCT-side TU),
/// and places each constituent solid into a dd4hep::Assembly volume using a
/// TGeo bounding-box placeholder for each solid.
///
/// @todo Phase 2: replace per-solid bounding-box placeholders with a proper
/// TGeo↔G4OCCTSolid bridge so that Geant4 navigation uses the exact OCCT
/// BRep geometry.

// Two header worlds must not meet in the same TU:
//   DD4hep → ROOT → TString.h         declares: extern void Printf(...)
//   G4OCCT → OCCT → Standard_CString.h declares: int Printf(...)
// Keeping them in separate TUs (firewall pattern) resolves both this Printf
// conflict and the Handle(Class) macro collision.
// G4OCCT_STEPAssembly_impl.{hh,cc} is the OCCT-side TU; this file is the
// DD4hep-side TU.
#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/Printout.h>

#include "G4OCCT_STEPAssembly_impl.hh"

#include <G4Material.hh>
#include <G4NistManager.hh>

#include <map>
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

  // ── Build the material map from the compact XML entries ───────────────────
  std::map<std::string, G4Material*> materials;
  for (xml_coll_t it(x_map, _Unicode(entry)); it; ++it) {
    xml_comp_t x_entry = it;
    std::string stepName      = x_entry.attr<std::string>(_Unicode(step_name));
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
    materials[stepName] = g4mat;
    printout(DEBUG, "G4OCCT_STEPAssembly",
             "Mapped STEP material '%s' → G4Material '%s'",
             stepName.c_str(), dd4hepMatName.c_str());
  }

  // ── Import the STEP assembly (OCCT side, separate TU) ────────────────────
  int nConstituents = G4OCCT_ImportSTEPAssembly(path, materials);

  // ── Create a DD4hep assembly and place constituents ──────────────────────
  Assembly dd4hepAssembly(name + "_assembly");

  printout(INFO, "G4OCCT_STEPAssembly",
           "Imported '%s' from '%s'; %d constituent solid(s) in dd4hep::Assembly '%s'",
           name.c_str(), path.c_str(), nConstituents,
           (name + "_assembly").c_str());

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
