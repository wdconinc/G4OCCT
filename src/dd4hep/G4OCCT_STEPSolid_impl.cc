// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPSolid_impl.cc
/// @brief OCCT-side implementation of the STEP solid import bridge.
///
/// This TU includes G4OCCT and OpenCASCADE headers and must NOT include any
/// DD4hep or ROOT headers to avoid the Printf return-type collision (see
/// G4OCCT_STEPSolid_impl.hh).

#include "G4OCCT_STEPSolid_impl.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <stdexcept>
#include <string>

G4OCCT_STEPSolidGeometry G4OCCT_ImportSTEPSolid(const std::string& name,
                                                  const std::string& path)
{
  G4OCCTSolid* solid = nullptr;
  try {
    solid = G4OCCTSolid::FromSTEP(name, path);
  } catch (const std::exception& ex) {
    throw std::runtime_error("G4OCCT_STEPSolid: failed to import '" + path +
                             "' (" + ex.what() + ")");
  }

  // Copy the shape out before deleting the solid so we don't hold a reference
  // into a deleted object.  G4OCCTSolid::FromSTEP() returns a heap-allocated
  // object owned by the caller; the Geant4 solid store keeps a separate entry.
  const TopoDS_Shape shape = solid->GetOCCTShape();
  delete solid;
  solid = nullptr;

  // Tessellate with 1 % relative deflection, matching G4OCCTSolid::CreatePolyhedron().
  static constexpr double kRelativeDeflection = 0.01;
  BRepMesh_IncrementalMesh mesher(shape, kRelativeDeflection,
                                  /*isRelative=*/Standard_True);
  (void)mesher;

  G4OCCT_STEPSolidGeometry result;

  for (TopExp_Explorer explorer(shape, TopAbs_FACE);
       explorer.More(); explorer.Next())
  {
    const TopoDS_Face& face = TopoDS::Face(explorer.Current());
    TopLoc_Location    location;
    const Handle(Poly_Triangulation)& tri =
        BRep_Tool::Triangulation(face, location);
    if (tri.IsNull() || tri->NbTriangles() == 0) {
      continue;
    }

    const gp_Trsf& transform     = location.Transformation();
    const bool     reverseWinding = (face.Orientation() == TopAbs_REVERSED);

    for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i) {
      Standard_Integer n1, n2, n3;
      tri->Triangle(i).Get(n1, n2, n3);
      if (reverseWinding) {
        std::swap(n2, n3);
      }

      const gp_Pnt p1 = tri->Node(n1).Transformed(transform);
      const gp_Pnt p2 = tri->Node(n2).Transformed(transform);
      const gp_Pnt p3 = tri->Node(n3).Transformed(transform);

      G4OCCT_Triangle facet;
      facet.v[0] = {p1.X(), p1.Y(), p1.Z()};
      facet.v[1] = {p2.X(), p2.Y(), p2.Z()};
      facet.v[2] = {p3.X(), p3.Y(), p3.Z()};
      result.triangles.push_back(facet);
    }
  }

  if (result.triangles.empty()) {
    throw std::runtime_error(
        "G4OCCT_STEPSolid: tessellation of '" + path + "' produced no triangles");
  }
  return result;
}
