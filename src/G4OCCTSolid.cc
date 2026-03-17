// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTSolid.cc
/// @brief Implementation of G4OCCTSolid.

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

#include <optional>
#include <utility>

namespace {

/// Fall-back minimum extent coordinate used when the shape is null or void.
constexpr G4double kFallbackExtentMin = -1.0;
/// Fall-back maximum extent coordinate used when the shape is null or void.
constexpr G4double kFallbackExtentMax = 1.0;

/// Convert a Geant4 three-vector to an OCCT point.
gp_Pnt ToPoint(const G4ThreeVector& point) { return gp_Pnt(point.x(), point.y(), point.z()); }

/// Return the surface-intersection tolerance derived from the Geant4 geometry tolerance.
G4double IntersectionTolerance() {
  return 0.5 * G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();
}

/// Build an OCCT vertex from a Geant4 three-vector using the intersection tolerance.
TopoDS_Vertex MakeVertex(const G4ThreeVector& point) {
  BRep_Builder builder;
  TopoDS_Vertex vertex;
  builder.MakeVertex(vertex, ToPoint(point), IntersectionTolerance());
  return vertex;
}

/// Map an OCCT solid-classifier state to the corresponding Geant4 inside enum.
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

/// Return the canonical fall-back surface normal (positive Z axis).
G4ThreeVector FallbackNormal() { return G4ThreeVector(0.0, 0.0, 1.0); }

/// Compute the shortest distance from @p point to the surface of @p shape.
/// Returns kInfinity if the shape is null or the calculation fails.
G4double DistanceFromPointToShape(const TopoDS_Shape& shape, const G4ThreeVector& point) {
  if (shape.IsNull()) {
    return kInfinity;
  }

  BRepExtrema_DistShapeShape distance(MakeVertex(point), shape);
  if (!distance.IsDone() || distance.NbSolution() == 0) {
    return kInfinity;
  }

  const G4double shortestDistance = distance.Value();
  return (shortestDistance <= IntersectionTolerance()) ? 0.0 : shortestDistance;
}

/// Evaluate the outward surface normal on @p face at parameters (@p u, @p v).
/// Returns the normal vector on success, or std::nullopt if the normal is undefined.
std::optional<G4ThreeVector> TryGetOutwardNormal(const TopoDS_Face& face, const Standard_Real u,
                                                 const Standard_Real v) {
  BRepAdaptor_Surface surface(face);
  Standard_Real adjustedU = u;
  Standard_Real adjustedV = v;
  const Standard_Real tolerance = IntersectionTolerance();
  if (surface.IsUPeriodic()) {
    const Standard_Real uEpsilon =
        std::max(surface.UResolution(tolerance), Precision::PConfusion());
    const Standard_Real uFirst = surface.FirstUParameter();
    const Standard_Real uLast  = surface.LastUParameter();
    if (std::abs(adjustedU - uFirst) <= uEpsilon) {
      adjustedU = uFirst + uEpsilon;
    } else if (std::abs(adjustedU - uLast) <= uEpsilon) {
      adjustedU = uLast - uEpsilon;
    }
  }
  if (surface.IsVPeriodic()) {
    const Standard_Real vEpsilon =
        std::max(surface.VResolution(tolerance), Precision::PConfusion());
    const Standard_Real vFirst = surface.FirstVParameter();
    const Standard_Real vLast  = surface.LastVParameter();
    if (std::abs(adjustedV - vFirst) <= vEpsilon) {
      adjustedV = vFirst + vEpsilon;
    } else if (std::abs(adjustedV - vLast) <= vEpsilon) {
      adjustedV = vLast - vEpsilon;
    }
  }

  BRepLProp_SLProps props(surface, adjustedU, adjustedV, 1, tolerance);
  if (!props.IsNormalDefined()) {
    return std::nullopt;
  }

  gp_Dir faceNormal = props.Normal();
  if (face.Orientation() == TopAbs_REVERSED) {
    faceNormal.Reverse();
  }

  return G4ThreeVector(faceNormal.X(), faceNormal.Y(), faceNormal.Z());
}

struct ClosestFaceMatch {
  TopoDS_Face face;
  G4double distance{kInfinity};
};

/// Find the closest trimmed face to @p point without triggering solid-level classification.
std::optional<ClosestFaceMatch> TryFindClosestFace(const TopoDS_Shape& shape,
                                                   const G4ThreeVector& point) {
  if (shape.IsNull()) {
    return std::nullopt;
  }

  const TopoDS_Vertex queryVertex = MakeVertex(point);
  std::optional<ClosestFaceMatch> bestMatch;
  for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    const TopoDS_Face face = TopoDS::Face(explorer.Current());
    BRepExtrema_DistShapeShape distance(queryVertex, face);
    if (!distance.IsDone() || distance.NbSolution() == 0) {
      continue;
    }

    const G4double candidateDistance = distance.Value();
    if (bestMatch.has_value() && candidateDistance >= bestMatch->distance) {
      continue;
    }

    for (Standard_Integer solution = 1; solution <= distance.NbSolution(); ++solution) {
      const BRepExtrema_SupportType supportType = distance.SupportTypeShape2(solution);
      if (supportType != BRepExtrema_IsInFace && supportType != BRepExtrema_IsOnEdge) {
        continue;
      }
      bestMatch = ClosestFaceMatch{face, candidateDistance};
      break;
    }
  }

  return bestMatch;
}

} // namespace

G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape) {
  ComputeBounds();
}

// ── G4OCCTSolid private helpers ───────────────────────────────────────────────

void G4OCCTSolid::ComputeBounds() {
  if (fShape.IsNull()) {
    fCachedBounds = std::nullopt;
    return;
  }

  Bnd_Box boundingBox;
  BRepBndLib::Add(fShape, boundingBox);
  if (boundingBox.IsVoid()) {
    fCachedBounds = std::nullopt;
    return;
  }

  Standard_Real xMin = 0.0;
  Standard_Real yMin = 0.0;
  Standard_Real zMin = 0.0;
  Standard_Real xMax = 0.0;
  Standard_Real yMax = 0.0;
  Standard_Real zMax = 0.0;
  boundingBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);

  fCachedBounds =
      AxisAlignedBounds{G4ThreeVector(xMin, yMin, zMin), G4ThreeVector(xMax, yMax, zMax)};
}

BRepClass3d_SolidClassifier& G4OCCTSolid::GetOrCreateClassifier() const {
  ClassifierCache& cache         = fClassifierCache.Get();
  const std::uint64_t currentGen = fShapeGeneration.load(std::memory_order_acquire);
  if (cache.generation != currentGen) {
    cache.classifier.emplace();
    cache.classifier->Load(fShape); // O(N_faces) — paid once per thread per shape
    cache.generation = currentGen;
  }
  return *cache.classifier;
}

IntCurvesFace_ShapeIntersector& G4OCCTSolid::GetOrCreateIntersector() const {
  IntersectorCache& cache        = fIntersectorCache.Get();
  const std::uint64_t currentGen = fShapeGeneration.load(std::memory_order_acquire);
  if (cache.generation != currentGen) {
    cache.intersector.emplace();
    cache.intersector->Load(fShape,
                            IntersectionTolerance()); // O(N_faces) — paid once per thread per shape
    cache.generation = currentGen;
  }
  return *cache.intersector;
}

// ── G4VSolid pure-virtual implementations ────────────────────────────────────

EInside G4OCCTSolid::Inside(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return kOutside;
  }

  const G4double tolerance = IntersectionTolerance();
  if (fCachedBounds.has_value()) {
    const AxisAlignedBounds& bounds = *fCachedBounds;
    if (p.x() < bounds.min.x() - tolerance || p.x() > bounds.max.x() + tolerance ||
        p.y() < bounds.min.y() - tolerance || p.y() > bounds.max.y() + tolerance ||
        p.z() < bounds.min.z() - tolerance || p.z() > bounds.max.z() + tolerance) {
      return kOutside;
    }
  }

  BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();
  classifier.Perform(ToPoint(p), tolerance);
  return ToG4Inside(classifier.State());
}

G4ThreeVector G4OCCTSolid::SurfaceNormal(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return FallbackNormal();
  }

  // Use face-local extrema + projection instead of a single solid-wide
  // BRepExtrema_DistShapeShape(vertex, solid) query. On imported periodic
  // surfaces (notably the torus seam hit at (15,0,0) in the geometry-fixture
  // tests), the solid-wide path can descend into OCCT's 2D seam classifier and
  // either wedge in boundary classification or report an edge-supported
  // solution whose direct normal evaluation falls back to +Z. Face-local
  // selection still identifies the nearest supporting face, while the explicit
  // projection step recovers a usable (u,v) pair that we can nudge off exact
  // periodic seams before asking OCCT for derivatives. Revisit this once the
  // upstream periodic-classifier fix is available everywhere we build.
  const auto closestFaceMatch = TryFindClosestFace(fShape, p);
  if (!closestFaceMatch.has_value()) {
    return FallbackNormal();
  }

  TopLoc_Location loc;
  const Handle(Geom_Surface) surface = BRep_Tool::Surface(closestFaceMatch->face, loc);
  if (surface.IsNull()) {
    return FallbackNormal();
  }

  gp_Pnt pLocal = ToPoint(p);
  if (!loc.IsIdentity()) {
    pLocal.Transform(loc.Transformation().Inverted());
  }

  GeomAPI_ProjectPointOnSurf projection(pLocal, surface);
  if (projection.NbPoints() == 0) {
    return FallbackNormal();
  }

  Standard_Real u = 0.0;
  Standard_Real v = 0.0;
  projection.LowerDistanceParameters(u, v);

  const auto normal =
      TryGetOutwardNormal(closestFaceMatch->face, u, v);
  return normal.value_or(FallbackNormal());
}

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p, const G4ThreeVector& v) const {
  if (fShape.IsNull() || v.mag2() == 0.0) {
    return kInfinity;
  }

  IntCurvesFace_ShapeIntersector& intersector = GetOrCreateIntersector();

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

  BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();
  classifier.Perform(ToPoint(p), IntersectionTolerance());
  if (classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON) {
    return 0.0;
  }

  return DistanceFromPointToShape(fShape, p);
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v,
                                    const G4bool calcNorm, G4bool* validNorm,
                                    G4ThreeVector* n) const {
  if (validNorm != nullptr) {
    *validNorm = false;
  }

  if (fShape.IsNull() || v.mag2() == 0.0) {
    return 0.0;
  }

  const G4double tolerance = IntersectionTolerance();
  const gp_Lin ray(ToPoint(p), gp_Dir(v.x(), v.y(), v.z()));

  IntCurvesFace_ShapeIntersector& intersector = GetOrCreateIntersector();
  intersector.Perform(ray, tolerance, Precision::Infinite());

  if (!intersector.IsDone() || intersector.NbPnt() == 0) {
    return 0.0;
  }

  G4double minDistance      = kInfinity;
  Standard_Integer minIndex = -1;
  for (Standard_Integer index = 1; index <= intersector.NbPnt(); ++index) {
    const G4double candidateDistance = intersector.WParameter(index);
    if (candidateDistance > tolerance && candidateDistance < minDistance) {
      minDistance = candidateDistance;
      minIndex    = index;
    }
  }

  if (minIndex < 1 || minDistance == kInfinity) {
    return 0.0;
  }

  if (calcNorm && validNorm != nullptr && n != nullptr &&
      intersector.State(minIndex) == TopAbs_IN) {
    if (auto outNorm =
            TryGetOutwardNormal(intersector.Face(minIndex), intersector.UParameter(minIndex),
                                intersector.VParameter(minIndex))) {
      *n         = *outNorm;
      *validNorm = true;
    }
  }

  return minDistance;
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return 0.0;
  }

  const TopoDS_Vertex vertex = MakeVertex(p);
  G4double minDistance       = kInfinity;
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

G4GeometryType G4OCCTSolid::GetEntityType() const { return "G4OCCTSolid"; }

G4VisExtent G4OCCTSolid::GetExtent() const {
  if (!fCachedBounds) {
    return G4VisExtent(kFallbackExtentMin, kFallbackExtentMax, kFallbackExtentMin,
                       kFallbackExtentMax, kFallbackExtentMin, kFallbackExtentMax);
  }

  return G4VisExtent(fCachedBounds->min.x(), fCachedBounds->max.x(), fCachedBounds->min.y(),
                     fCachedBounds->max.y(), fCachedBounds->min.z(), fCachedBounds->max.z());
}

void G4OCCTSolid::BoundingLimits(G4ThreeVector& pMin, G4ThreeVector& pMax) const {
  if (!fCachedBounds) {
    pMin = G4ThreeVector(kFallbackExtentMin, kFallbackExtentMin, kFallbackExtentMin);
    pMax = G4ThreeVector(kFallbackExtentMax, kFallbackExtentMax, kFallbackExtentMax);
    return;
  }
  pMin = fCachedBounds->min;
  pMax = fCachedBounds->max;
}

G4bool G4OCCTSolid::CalculateExtent(const EAxis pAxis, const G4VoxelLimits& pVoxelLimit,
                                    const G4AffineTransform& pTransform, G4double& pMin,
                                    G4double& pMax) const {
  if (!fCachedBounds) {
    return false;
  }

  const G4BoundingEnvelope envelope(fCachedBounds->min, fCachedBounds->max);
  return envelope.CalculateExtent(pAxis, pVoxelLimit, G4Transform3D(pTransform), pMin, pMax);
}

void G4OCCTSolid::DescribeYourselfTo(G4VGraphicsScene& scene) const { scene.AddSolid(*this); }

G4Polyhedron* G4OCCTSolid::CreatePolyhedron() const {
  if (fShape.IsNull()) {
    return nullptr;
  }

  // Use a relative deflection (1 % of each face's bounding-box size) so that
  // the mesh density scales with the shape rather than being a fixed world-
  // space length that is inappropriate for both very small and very large
  // shapes.
  constexpr Standard_Real kRelativeDeflection = 0.01;
  BRepMesh_IncrementalMesh mesher(fShape, kRelativeDeflection, /*isRelative=*/Standard_True);
  (void)mesher;

  G4TessellatedSolid tessellatedSolid(GetName() + "_polyhedron");
  G4int facetCount = 0;

  for (TopExp_Explorer explorer(fShape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    const TopoDS_Face& face = TopoDS::Face(explorer.Current());
    TopLoc_Location location;
    const Handle(Poly_Triangulation) & triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull()) {
      continue;
    }

    const gp_Trsf& transform  = location.Transformation();
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

      auto* facet =
          new G4TriangularFacet(G4ThreeVector(point1.X(), point1.Y(), point1.Z()),
                                G4ThreeVector(point2.X(), point2.Y(), point2.Z()),
                                G4ThreeVector(point3.X(), point3.Y(), point3.Z()), ABSOLUTE);
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
