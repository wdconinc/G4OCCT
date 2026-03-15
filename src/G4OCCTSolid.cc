// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_State.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <G4BoundingEnvelope.hh>
#include <G4AffineTransform.hh>
#include <G4GeometryTolerance.hh>
#include <G4Polyhedron.hh>
#include <G4TessellatedSolid.hh>
#include <G4TriangularFacet.hh>
#include <G4VGraphicsScene.hh>
#include <G4VisExtent.hh>

#include <utility>

namespace {

constexpr Standard_Real kMinNormalMagnitudeSquared = 1.0e-24;
constexpr G4double kFallbackExtentMin = -1.0;
constexpr G4double kFallbackExtentMax = 1.0;

struct AxisAlignedBounds {
  G4ThreeVector min;
  G4ThreeVector max;
  bool isVoid;
};

gp_Pnt ToPoint(const G4ThreeVector& point) {
  return gp_Pnt(point.x(), point.y(), point.z());
}

G4double IntersectionTolerance() {
  return 0.5 * G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();
}

TopoDS_Vertex MakeVertex(const G4ThreeVector& point) {
  BRep_Builder builder;
  TopoDS_Vertex vertex;
  builder.MakeVertex(vertex, ToPoint(point), IntersectionTolerance());
  return vertex;
}

EInside ToG4Inside(const TopAbs_State state) {
  switch (state) {
    case TopAbs_IN:
      return kInside;
    case TopAbs_ON:
      return kSurface;
    case TopAbs_OUT:
    default:
      return kOutside;
  }
}

G4ThreeVector FallbackNormal() {
  return G4ThreeVector(0.0, 0.0, 1.0);
}

AxisAlignedBounds ComputeAxisAlignedBounds(const TopoDS_Shape& shape) {
  if (shape.IsNull()) {
    return {G4ThreeVector(kFallbackExtentMin, kFallbackExtentMin, kFallbackExtentMin),
            G4ThreeVector(kFallbackExtentMax, kFallbackExtentMax, kFallbackExtentMax), true};
  }

  Bnd_Box boundingBox;
  BRepBndLib::Add(shape, boundingBox);
  if (boundingBox.IsVoid()) {
    return {G4ThreeVector(kFallbackExtentMin, kFallbackExtentMin, kFallbackExtentMin),
            G4ThreeVector(kFallbackExtentMax, kFallbackExtentMax, kFallbackExtentMax), true};
  }

  Standard_Real xMin = 0.0;
  Standard_Real yMin = 0.0;
  Standard_Real zMin = 0.0;
  Standard_Real xMax = 0.0;
  Standard_Real yMax = 0.0;
  Standard_Real zMax = 0.0;
  boundingBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);

  return {G4ThreeVector(xMin, yMin, zMin), G4ThreeVector(xMax, yMax, zMax), false};
}

G4double DistanceFromPointToShape(const TopoDS_Shape& shape, const G4ThreeVector& point) {
  if (shape.IsNull()) {
    return kInfinity;
  }

  BRepExtrema_DistShapeShape distance(MakeVertex(point), shape);
  if (!distance.IsDone()) {
    return kInfinity;
  }

  const G4double shortestDistance = distance.Value();
  return (shortestDistance <= IntersectionTolerance()) ? 0.0 : shortestDistance;
}

bool TryGetOutwardNormal(const TopoDS_Face& face,
                         const Standard_Real u,
                         const Standard_Real v,
                         G4ThreeVector* normal) {
  if (normal == nullptr) {
    return false;
  }

  BRepAdaptor_Surface surface(face);
  BRepLProp_SLProps props(surface, u, v, 1, IntersectionTolerance());
  if (!props.IsNormalDefined()) {
    return false;
  }

  gp_Dir faceNormal = props.Normal();
  if (face.Orientation() == TopAbs_REVERSED) {
    faceNormal.Reverse();
  }

  *normal = G4ThreeVector(faceNormal.X(), faceNormal.Y(), faceNormal.Z());
  return true;
}

}  // namespace

G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape) {}

// ── G4VSolid pure-virtual implementations ────────────────────────────────────

EInside G4OCCTSolid::Inside(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return kOutside;
  }

  BRepClass3d_SolidClassifier classifier(fShape);
  classifier.Perform(ToPoint(p), IntersectionTolerance());
  return ToG4Inside(classifier.State());
}

G4ThreeVector G4OCCTSolid::SurfaceNormal(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return FallbackNormal();
  }

  BRepExtrema_DistShapeShape distance(MakeVertex(p), fShape);
  if (!distance.IsDone() || distance.NbSolution() == 0) {
    return FallbackNormal();
  }

  TopoDS_Face closestFace;
  for (Standard_Integer solution = 1; solution <= distance.NbSolution(); ++solution) {
    const TopoDS_Shape support = distance.SupportOnShape2(solution);
    if (support.ShapeType() == TopAbs_FACE) {
      closestFace = TopoDS::Face(support);
      break;
    }
  }

  if (closestFace.IsNull()) {
    return FallbackNormal();
  }

  const Handle(Geom_Surface) surface = BRep_Tool::Surface(closestFace);
  if (surface.IsNull()) {
    return FallbackNormal();
  }

  GeomAPI_ProjectPointOnSurf projection(ToPoint(p), surface);
  if (projection.NbPoints() == 0) {
    return FallbackNormal();
  }

  Standard_Real u = 0.0;
  Standard_Real v = 0.0;
  projection.LowerDistanceParameters(u, v);

  BRepAdaptor_Surface adaptor(closestFace);
  gp_Pnt projectedPoint;
  gp_Vec derivativeU;
  gp_Vec derivativeV;
  adaptor.D1(u, v, projectedPoint, derivativeU, derivativeV);

  gp_Vec normal = derivativeU.Crossed(derivativeV);
  if (normal.SquareMagnitude() < kMinNormalMagnitudeSquared) {
    return FallbackNormal();
  }

  normal.Normalize();
  if (closestFace.Orientation() == TopAbs_REVERSED) {
    normal.Reverse();
  }

  return G4ThreeVector(normal.X(), normal.Y(), normal.Z());
}

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p,
                                   const G4ThreeVector& v) const {
  if (fShape.IsNull() || v.mag2() == 0.0) {
    return kInfinity;
  }

  IntCurvesFace_ShapeIntersector intersector;
  intersector.Load(fShape, IntersectionTolerance());

  const gp_Lin ray(ToPoint(p), gp_Dir(v.x(), v.y(), v.z()));
  intersector.Perform(ray, IntersectionTolerance(), Precision::Infinite());

  if (!intersector.IsDone() || intersector.NbPnt() == 0) {
    return kInfinity;
  }

  G4double minDistance = kInfinity;
  for (Standard_Integer index = 1; index <= intersector.NbPnt(); ++index) {
    const G4double candidateDistance = intersector.WParameter(index);
    if (candidateDistance > IntersectionTolerance() && candidateDistance < minDistance) {
      minDistance = candidateDistance;
    }
  }

  return minDistance;
}

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return kInfinity;
  }

  BRepClass3d_SolidClassifier classifier(fShape);
  classifier.Perform(ToPoint(p), IntersectionTolerance());
  if (classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON) {
    return 0.0;
  }

  return DistanceFromPointToShape(fShape, p);
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p,
                                    const G4ThreeVector& v,
                                    const G4bool calcNorm,
                                    G4bool* validNorm,
                                    G4ThreeVector* n) const {
  if (validNorm != nullptr) {
    *validNorm = false;
  }

  if (fShape.IsNull() || v.mag2() == 0.0) {
    return 0.0;
  }

  const G4double tolerance = IntersectionTolerance();
  const gp_Lin ray(ToPoint(p), gp_Dir(v.x(), v.y(), v.z()));

  IntCurvesFace_ShapeIntersector intersector;
  intersector.Load(fShape, tolerance);
  intersector.Perform(ray, tolerance, Precision::Infinite());

  if (!intersector.IsDone() || intersector.NbPnt() == 0) {
    return 0.0;
  }

  G4double minDistance = kInfinity;
  Standard_Integer minIndex = -1;
  for (Standard_Integer index = 1; index <= intersector.NbPnt(); ++index) {
    const G4double candidateDistance = intersector.WParameter(index);
    if (candidateDistance > tolerance && candidateDistance < minDistance) {
      minDistance = candidateDistance;
      minIndex = index;
    }
  }

  if (minIndex < 1 || minDistance == kInfinity) {
    return 0.0;
  }

  if (calcNorm && validNorm != nullptr && n != nullptr &&
      intersector.State(minIndex) == TopAbs_IN &&
      TryGetOutwardNormal(intersector.Face(minIndex), intersector.UParameter(minIndex),
                          intersector.VParameter(minIndex), n)) {
    *validNorm = true;
  }

  return minDistance;
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return 0.0;
  }

  const TopoDS_Vertex vertex = MakeVertex(p);
  G4double minDistance = kInfinity;
  for (TopExp_Explorer explorer(fShape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    BRepExtrema_DistShapeShape distance(vertex, explorer.Current());
    if (!distance.IsDone() || distance.NbSolution() == 0) {
      continue;
    }

    if (distance.Value() < minDistance) {
      minDistance = distance.Value();
    }
  }

  return (minDistance < kInfinity) ? minDistance : 0.0;
}

G4GeometryType G4OCCTSolid::GetEntityType() const {
  return "G4OCCTSolid";
}

G4VisExtent G4OCCTSolid::GetExtent() const {
  const AxisAlignedBounds bounds = ComputeAxisAlignedBounds(fShape);
  if (bounds.isVoid) {
    return G4VisExtent(kFallbackExtentMin, kFallbackExtentMax, kFallbackExtentMin,
                       kFallbackExtentMax, kFallbackExtentMin, kFallbackExtentMax);
  }

  return G4VisExtent(bounds.min.x(), bounds.max.x(), bounds.min.y(), bounds.max.y(),
                     bounds.min.z(), bounds.max.z());
}

void G4OCCTSolid::BoundingLimits(G4ThreeVector& pMin, G4ThreeVector& pMax) const {
  const AxisAlignedBounds bounds = ComputeAxisAlignedBounds(fShape);
  pMin = bounds.min;
  pMax = bounds.max;
}

G4bool G4OCCTSolid::CalculateExtent(const EAxis pAxis,
                                    const G4VoxelLimits& pVoxelLimit,
                                    const G4AffineTransform& pTransform,
                                    G4double& pMin,
                                    G4double& pMax) const {
  const AxisAlignedBounds bounds = ComputeAxisAlignedBounds(fShape);
  if (bounds.isVoid) {
    return false;
  }

  const G4BoundingEnvelope envelope(bounds.min, bounds.max);
  return envelope.CalculateExtent(pAxis, pVoxelLimit, G4Transform3D(pTransform), pMin, pMax);
}

void G4OCCTSolid::DescribeYourselfTo(G4VGraphicsScene& scene) const {
  scene.AddSolid(*this);
}

G4Polyhedron* G4OCCTSolid::CreatePolyhedron() const {
  if (fShape.IsNull()) {
    return nullptr;
  }

  constexpr Standard_Real kLinearDeflection = 0.1;
  BRepMesh_IncrementalMesh mesher(fShape, kLinearDeflection);
  (void)mesher;

  G4TessellatedSolid tessellatedSolid(GetName() + "_polyhedron");
  G4int facetCount = 0;

  for (TopExp_Explorer explorer(fShape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    const TopoDS_Face& face = TopoDS::Face(explorer.Current());
    TopLoc_Location location;
    const Handle(Poly_Triangulation)& triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull()) {
      continue;
    }

    const gp_Trsf& transform = location.Transformation();
    const bool reverseWinding = face.Orientation() == TopAbs_REVERSED;

    for (Standard_Integer triangleIndex = 1; triangleIndex <= triangulation->NbTriangles();
         ++triangleIndex) {
      Standard_Integer index1 = 0;
      Standard_Integer index2 = 0;
      Standard_Integer index3 = 0;
      triangulation->Triangle(triangleIndex).Get(index1, index2, index3);

      if (reverseWinding) {
        std::swap(index2, index3);
      }

      const gp_Pnt point1 = triangulation->Node(index1).Transformed(transform);
      const gp_Pnt point2 = triangulation->Node(index2).Transformed(transform);
      const gp_Pnt point3 = triangulation->Node(index3).Transformed(transform);

      auto* facet = new G4TriangularFacet(G4ThreeVector(point1.X(), point1.Y(), point1.Z()),
                                          G4ThreeVector(point2.X(), point2.Y(), point2.Z()),
                                          G4ThreeVector(point3.X(), point3.Y(), point3.Z()),
                                          ABSOLUTE);
      if (!facet->IsDefined() || !tessellatedSolid.AddFacet(facet)) {
        delete facet;
        continue;
      }

      ++facetCount;
    }
  }

  if (facetCount == 0) {
    return nullptr;
  }

  tessellatedSolid.SetSolidClosed(true);
  G4Polyhedron* polyhedron = tessellatedSolid.GetPolyhedron();
  if (polyhedron == nullptr) {
    return nullptr;
  }

  return new G4Polyhedron(*polyhedron);
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
