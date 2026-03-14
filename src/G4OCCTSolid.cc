// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "G4OCCT/G4OCCTSolid.hh"

#include <G4Polyhedron.hh>
#include <G4VGraphicsScene.hh>
#include <G4VisExtent.hh>

G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape) {}

// ── G4VSolid pure-virtual implementations (stubs) ────────────────────────────

EInside G4OCCTSolid::Inside(const G4ThreeVector& /*p*/) const {
  // TODO: Use BRep_Builder / BRepClass3d_SolidClassifier to classify the point
  return kOutside;
}

G4ThreeVector G4OCCTSolid::SurfaceNormal(const G4ThreeVector& /*p*/) const {
  // TODO: Use BRepGProp_Face to obtain the surface normal at the closest face
  return G4ThreeVector(0, 0, 1);
}

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& /*p*/,
                                    const G4ThreeVector& /*v*/) const {
  // TODO: Use BRepIntCurveSurface_Inter or IntCurvesFace_ShapeIntersector
  return kInfinity;
}

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& /*p*/) const {
  // TODO: Use BRep_Tool and BRepExtrema_DistShapeShape
  return kInfinity;
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& /*p*/,
                                     const G4ThreeVector& /*v*/,
                                     const G4bool /*calcNorm*/,
                                     G4bool* validNorm,
                                     G4ThreeVector* /*n*/) const {
  // TODO: Use IntCurvesFace_ShapeIntersector for ray-shape intersection
  if (validNorm) { *validNorm = false; }
  return 0.0;
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& /*p*/) const {
  // TODO: Use BRepExtrema_DistShapeShape
  return 0.0;
}

G4GeometryType G4OCCTSolid::GetEntityType() const {
  return "G4OCCTSolid";
}

G4VisExtent G4OCCTSolid::GetExtent() const {
  // TODO: Use Bnd_Box + BRepBndLib::Add to compute the axis-aligned bounding
  //       box from the OCCT shape
  return G4VisExtent(-1, 1, -1, 1, -1, 1);
}

G4bool G4OCCTSolid::CalculateExtent(const EAxis /*pAxis*/,
                                    const G4VoxelLimits& /*pVoxelLimit*/,
                                    const G4AffineTransform& /*pTransform*/,
                                    G4double& pMin, G4double& pMax) const {
  // TODO: Use Bnd_Box + BRepBndLib::Add with transformation
  pMin = -1.0;
  pMax = 1.0;
  return true;
}

void G4OCCTSolid::DescribeYourselfTo(G4VGraphicsScene& scene) const {
  scene.AddSolid(*this);
}

G4Polyhedron* G4OCCTSolid::CreatePolyhedron() const {
  // TODO: Tessellate the OCCT shape with BRepMesh_IncrementalMesh and convert
  //       the resulting triangulation to a G4Polyhedron
  return nullptr;
}

std::ostream& G4OCCTSolid::StreamInfo(std::ostream& os) const {
  os << "-----------------------------------------------------------\n"
     << "    *** Dump for solid - " << GetName() << " ***\n"
     << "    ===================================================\n"
     << " Solid type: G4OCCTSolid\n"
     << " OCCT shape type: " << fShape.ShapeType() << "\n"
     << "-----------------------------------------------------------\n";
  return os;
}
