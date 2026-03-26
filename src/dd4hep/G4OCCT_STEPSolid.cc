// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPSolid.cc
/// @brief DD4hep detector-element plugin: single solid from a STEP file.
///
/// Compact XML usage:
/// @code{.xml}
/// <detector id="1" name="MyBeamPipe" type="G4OCCT_STEPSolid" vis="BlueVis">
///   <step_file path="geometry/beampipe.step"/>
///   <position x="0" y="0" z="0"/>
///   <rotation x="0" y="0" z="0"/>
///   <material name="Aluminium"/>
/// </detector>
/// @endcode
///
/// Phase 1 implementation notes
/// ----------------------------
/// The plugin imports the STEP file via G4OCCTSolid::FromSTEP, then uses the
/// solid's axis-aligned bounding box to create a dd4hep::Box placeholder for
/// the TGeo representation.  When the simulation runs with the Geant4 backend,
/// the converted solid will be a G4Box matching the bounding envelope.
///
/// @todo Phase 2: replace the TGeo bounding-box placeholder with a proper
/// TGeo↔G4OCCTSolid bridge so that Geant4 navigation uses the exact OCCT
/// BRep geometry rather than the bounding box approximation.

// Two header worlds must not meet in the same TU:
//   DD4hep → ROOT → TString.h         declares: extern void Printf(...)
//   G4OCCT → OCCT → Standard_CString.h declares: int Printf(...)
// Keeping them in separate TUs (firewall pattern) resolves both this Printf
// conflict and the Handle(Class) macro collision described above.
// G4OCCT_STEPSolid_impl.{hh,cc} is the OCCT-side TU; this file is the
// DD4hep-side TU.
#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/Printout.h>

#include "G4OCCT_STEPSolid_impl.hh"

#include <stdexcept>
#include <string>

using namespace dd4hep;

static Ref_t create_step_solid(Detector& description, xml_h e,
                               SensitiveDetector /*sens*/)
{
  xml_det_t  x_det  = e;
  xml_comp_t x_step = x_det.child(_Unicode(step_file));
  xml_comp_t x_pos  = x_det.child(_Unicode(position));
  xml_comp_t x_rot  = x_det.child(_Unicode(rotation));
  xml_comp_t x_mat  = x_det.child(_Unicode(material));

  std::string name = x_det.nameStr();
  std::string path = x_step.attr<std::string>(_Unicode(path));

  // ── Import STEP solid (OCCT side, separate TU) ───────────────────────────
  // Phase 1: use the axis-aligned bounding box as the TGeo solid.
  // G4VSolid::BoundingLimits returns the AABB corners in Geant4's native
  // unit system (mm).  DD4hep (with Geant4 backend) also works in mm, so
  // no unit conversion is needed.
  G4OCCT_STEPSolidGeometry geom = G4OCCT_ImportSTEPSolid(name, path);

  printout(INFO, "G4OCCT_STEPSolid",
           "Imported '%s' from '%s'; bounding box [%.3g, %.3g, %.3g] mm",
           name.c_str(), path.c_str(), geom.halfX, geom.halfY, geom.halfZ);

  // ── DD4hep volume and placement ──────────────────────────────────────────
  Material mat = description.material(x_mat.attr<std::string>(_Unicode(name)));
  Box      dd4hepBox(geom.halfX, geom.halfY, geom.halfZ);
  Volume   vol(name + "_vol", dd4hepBox, mat);
  vol.setVisAttributes(description, x_det.visStr());

  DetElement det(name, x_det.id());
  Position   pos(x_pos.x(), x_pos.y(), x_pos.z());
  RotationZYX rot(x_rot.z(), x_rot.y(), x_rot.x());
  PlacedVolume pv =
      description.pickMotherVolume(det).placeVolume(vol, Transform3D(rot, pos));
  pv.addPhysVolID("system", x_det.id());
  det.setPlacement(pv);
  return det;
}

DECLARE_DETELEMENT(G4OCCT_STEPSolid, create_step_solid)
