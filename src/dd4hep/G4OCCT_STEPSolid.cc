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
/// The plugin imports the STEP file via G4OCCTSolid::FromSTEP and tessellates
/// the resulting solid (1 % relative deflection) to produce a
/// dd4hep::TessellatedSolid for the TGeo representation.  This gives TGeo and
/// visualisation tools the actual surface geometry rather than a bounding-box
/// approximation.
///
/// @todo Phase 2: replace the TessellatedSolid with a proper TGeo↔G4OCCTSolid
/// bridge so that Geant4 navigation uses the exact OCCT BRep geometry for all
/// geometric queries (SurfaceNormal, DistanceToIn, etc.).

// Two header worlds must not meet in the same TU:
//   DD4hep → ROOT → TString.h         declares: extern void Printf(...)
//   G4OCCT → OCCT → Standard_CString.h declares: int Printf(...)
// Keeping them in separate TUs (firewall pattern) resolves both this Printf
// conflict and the Handle(Class) macro collision described above.
// G4OCCT_STEPSolid_impl.{hh,cc} is the OCCT-side TU; this file is the
// DD4hep-side TU.
#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/Printout.h>
#include <TGeoTessellated.h>

#include "G4OCCT_STEPSolid_impl.hh"

#include <cstddef>
#include <limits>
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

  // ── Import STEP solid and tessellate (OCCT side, separate TU) ───────────
  G4OCCT_STEPSolidGeometry geom = G4OCCT_ImportSTEPSolid(name, path);

  printout(INFO, "G4OCCT_STEPSolid",
           "Imported '%s' from '%s'; %zu triangles in tessellated solid",
           name.c_str(), path.c_str(), geom.triangles.size());

  // ── Build a TessellatedSolid for the TGeo/DD4hep representation ──────────
  // TessellatedSolid wraps ROOT's TGeoTessellated: a polyhedral mesh solid
  // that can be visualised and used for navigation by TGeo-based tools.
  // We pass the triangle count as a capacity hint, then add facets one by one.
  const std::size_t nTriangles = geom.triangles.size();
  if (nTriangles > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error(
        "G4OCCT_STEPSolid: tessellation of '" + path + "' produced " +
        std::to_string(nTriangles) +
        " triangles, which exceeds TessellatedSolid's int capacity");
  }
  TessellatedSolid tess(name + "_tess", static_cast<int>(nTriangles));
  for (const auto& tri : geom.triangles) {
    tess.addFacet(TessellatedSolid::Vertex(tri.v[0].x, tri.v[0].y, tri.v[0].z),
                  TessellatedSolid::Vertex(tri.v[1].x, tri.v[1].y, tri.v[1].z),
                  TessellatedSolid::Vertex(tri.v[2].x, tri.v[2].y, tri.v[2].z));
  }
  tess.ptr()->CloseShape(/*check=*/true, /*fixFlipped=*/true, /*verbose=*/false);

  // ── DD4hep volume and placement ──────────────────────────────────────────
  Material     mat = description.material(x_mat.attr<std::string>(_Unicode(name)));
  Volume       vol(name + "_vol", tess, mat);
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
