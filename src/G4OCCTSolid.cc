// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTSolid.cc
/// @brief Implementation of G4OCCTSolid.

#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <STEPControl_Reader.hxx>
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
#include <G4Exception.hh>
#include <G4GeometryTolerance.hh>
#include <G4Polyhedron.hh>
#include <G4TessellatedSolid.hh>
#include <G4TriangularFacet.hh>
#include <G4VGraphicsScene.hh>
#include <G4VisExtent.hh>
#include <Randomize.hh>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

/// Fall-back minimum extent coordinate used when the shape is null or void.
constexpr G4double kFallbackExtentMin = -1.0;
/// Fall-back maximum extent coordinate used when the shape is null or void.
constexpr G4double kFallbackExtentMax = 1.0;

/// Relative linear deflection used when tessellating the OCCT shape for
/// visualisation (`CreatePolyhedron`) and surface-point sampling
/// (`GetPointOnSurface`).  A value of 0.01 requests a chord height of at
/// most 1 % of each face's bounding-box size.
constexpr Standard_Real kRelativeDeflection = 0.01;

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
  Standard_Real adjustedU       = u;
  Standard_Real adjustedV       = v;
  const Standard_Real tolerance = IntersectionTolerance();
  // Nudge U away from parametric boundaries to avoid seam singularities.
  // For periodic surfaces this prevents descending into OCCT's 2D seam
  // classifier; for non-periodic surfaces with degenerate boundary edges
  // (e.g. the poles of a sphere) this ensures IsNormalDefined() returns true.
  {
    const Standard_Real uFirst   = surface.FirstUParameter();
    const Standard_Real uLast    = surface.LastUParameter();
    const Standard_Real uEpsilon = std::min(
        std::max(surface.UResolution(tolerance), Precision::PConfusion()), 0.5 * (uLast - uFirst));
    if (std::abs(adjustedU - uFirst) <= uEpsilon) {
      adjustedU = std::min(uFirst + uEpsilon, uLast);
    } else if (std::abs(adjustedU - uLast) <= uEpsilon) {
      adjustedU = std::max(uLast - uEpsilon, uFirst);
    }
  }
  // Nudge V away from parametric boundaries.  Non-periodic V boundaries
  // include degenerate pole edges on spheres (V = ±π/2) and cone apices.
  {
    const Standard_Real vFirst   = surface.FirstVParameter();
    const Standard_Real vLast    = surface.LastVParameter();
    const Standard_Real vEpsilon = std::min(
        std::max(surface.VResolution(tolerance), Precision::PConfusion()), 0.5 * (vLast - vFirst));
    if (std::abs(adjustedV - vFirst) <= vEpsilon) {
      adjustedV = std::min(vFirst + vEpsilon, vLast);
    } else if (std::abs(adjustedV - vLast) <= vEpsilon) {
      adjustedV = std::max(vLast - vEpsilon, vFirst);
    }
  }

  BRepLProp_SLProps props(surface, adjustedU, adjustedV, 1, tolerance);
  // If IsNormalDefined() fails the initial nudge may still be too small for
  // OCCT's internal null-derivative threshold (e.g. a sphere pole where
  // |dP/dU| = R·sin(δ) must exceed IntersectionTolerance()).  Retry with
  // exponentially larger V nudges, walking toward the V midpoint.
  if (!props.IsNormalDefined()) {
    const Standard_Real vFirst = surface.FirstVParameter();
    const Standard_Real vLast  = surface.LastVParameter();
    const Standard_Real vMid   = 0.5 * (vFirst + vLast);
    const bool nearVFirst      = (adjustedV < vMid);
    const Standard_Real vRes   = std::max(surface.VResolution(tolerance), Precision::PConfusion());
    Standard_Real finalRetryV  = adjustedV;
    for (int attempt = 0; attempt < 8 && !props.IsNormalDefined(); ++attempt) {
      const Standard_Real scale = std::pow(10.0, static_cast<Standard_Real>(attempt));
      const Standard_Real nudge = scale * vRes;
      finalRetryV =
          nearVFirst ? std::min(adjustedV + nudge, vMid) : std::max(adjustedV - nudge, vMid);
      props = BRepLProp_SLProps(surface, adjustedU, finalRetryV, 1, tolerance);
    }
    if (!props.IsNormalDefined()) {
      return std::nullopt;
    }
    // Guard: if the retry had to drift V by more than kMaxRetryVDriftFraction of
    // the full V range, the normal is sampled from a different surface region
    // (e.g. the equatorial band instead of the pole on a NURBS ellipsoid), which
    // would produce a spurious mismatch against the exact analytic normal, so we
    // treat the normal as unavailable at this point and return std::nullopt for
    // the caller to handle (e.g. by leaving the normal invalid or using a fallback).
    constexpr Standard_Real kMaxRetryVDriftFraction = 0.10;
    if (std::fabs(finalRetryV - adjustedV) > kMaxRetryVDriftFraction * (vLast - vFirst)) {
      return std::nullopt;
    }
  }

  gp_Dir faceNormal = props.Normal();
  if (face.Orientation() == TopAbs_REVERSED) {
    faceNormal.Reverse();
  }

  return G4ThreeVector(faceNormal.X(), faceNormal.Y(), faceNormal.Z());
}

} // namespace

G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape) {
  ComputeBounds();
}

// ── G4OCCTSolid static factory ────────────────────────────────────────────────

G4OCCTSolid* G4OCCTSolid::FromSTEP(const G4String& name, const std::string& path) {
  STEPControl_Reader reader;
  if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
    throw std::runtime_error("G4OCCTSolid::FromSTEP: failed to read STEP file: " + path);
  }
  if (reader.TransferRoots() <= 0) {
    throw std::runtime_error("G4OCCTSolid::FromSTEP: no roots transferred from: " + path);
  }
  TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) {
    throw std::runtime_error("G4OCCTSolid::FromSTEP: null shape loaded from: " + path);
  }
  return new G4OCCTSolid(name, shape);
}

// ── G4OCCTSolid private helpers ───────────────────────────────────────────────

void G4OCCTSolid::ComputeBounds() {
  if (fShape.IsNull()) {
    fCachedBounds = std::nullopt;
    BRep_Builder builder;
    builder.MakeCompound(fFaceCompound);
    fFaceBoundsCache.clear();
    return;
  }

  // Pre-build PCurves for edges on planar faces so BRep_Tool::CurveOnPlane()
  // returns the stored result on every Inside() query instead of recomputing
  // via GeomProjLib::ProjectOnPlane each time (~3.3% of total instructions).
  for (TopExp_Explorer faceEx(fShape, TopAbs_FACE); faceEx.More(); faceEx.Next()) {
    const TopoDS_Face& face = TopoDS::Face(faceEx.Current());
    if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane) {
      continue;
    }
    for (TopExp_Explorer edgeEx(face, TopAbs_EDGE); edgeEx.More(); edgeEx.Next()) {
      BRepLib::BuildPCurveForEdgeOnPlane(TopoDS::Edge(edgeEx.Current()), face);
    }
  }

  // Build a compound of all faces so that BRepExtrema_DistShapeShape queries
  // against fFaceCompound return the surface distance even for interior points
  // (a solid-wide query returns 0 for interior points).
  BRep_Builder builder;
  builder.MakeCompound(fFaceCompound);
  for (TopExp_Explorer ex(fShape, TopAbs_FACE); ex.More(); ex.Next()) {
    builder.Add(fFaceCompound, ex.Current());
  }

  Bnd_Box boundingBox;
  BRepBndLib::AddOptimal(fShape, boundingBox, /*useTriangulation=*/Standard_False);
  if (boundingBox.IsVoid()) {
    fCachedBounds = std::nullopt;
    fFaceBoundsCache.clear();
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

  // Build per-face bounding-box cache for the SurfaceNormal prefilter.
  fFaceBoundsCache.clear();
  for (TopExp_Explorer ex(fShape, TopAbs_FACE); ex.More(); ex.Next()) {
    Bnd_Box faceBox;
    BRepBndLib::AddOptimal(ex.Current(), faceBox, /*useTriangulation=*/Standard_False);
    fFaceBoundsCache.push_back({TopoDS::Face(ex.Current()), faceBox});
  }
}

std::optional<G4OCCTSolid::ClosestFaceMatch>
G4OCCTSolid::TryFindClosestFace(const std::vector<FaceBounds>& faceBoundsCache,
                                const G4ThreeVector& point) {
  if (faceBoundsCache.empty()) {
    return std::nullopt;
  }

  const gp_Pnt queryPoint         = ToPoint(point);
  const TopoDS_Vertex queryVertex = MakeVertex(point);

  // Build the point box once — it is constant for the entire loop.
  Bnd_Box queryBox;
  queryBox.Add(queryPoint);

  std::optional<ClosestFaceMatch> bestMatch;
  for (const FaceBounds& fb : faceBoundsCache) {
    // Lower bound: distance from query point to the face's axis-aligned bounding box.
    // If this is already >= current best, the face cannot be the closest — skip it.
    if (bestMatch.has_value() && fb.box.Distance(queryBox) >= bestMatch->distance) {
      continue;
    }

    BRepExtrema_DistShapeShape distance(queryVertex, fb.face);
    if (!distance.IsDone() || distance.NbSolution() == 0) {
      continue;
    }

    const G4double candidateDistance = distance.Value();
    if (bestMatch.has_value() && candidateDistance >= bestMatch->distance) {
      continue;
    }

    for (Standard_Integer solution = 1; solution <= distance.NbSolution(); ++solution) {
      const BRepExtrema_SupportType supportType = distance.SupportTypeShape2(solution);
      // Accept interior and boundary solutions.  Vertex support (e.g. the
      // degenerate south/north pole of a sphere) is also accepted: the
      // subsequent GeomAPI_ProjectPointOnSurf step obtains correct UV
      // parameters independently, and the pole-nudge logic in
      // TryGetOutwardNormal handles degenerate surface points.
      if (supportType != BRepExtrema_IsInFace && supportType != BRepExtrema_IsOnEdge &&
          supportType != BRepExtrema_IsVertex) {
        continue;
      }
      bestMatch = ClosestFaceMatch{fb.face, candidateDistance};
      break;
    }
  }

  return bestMatch;
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
  const auto closestFaceMatch = TryFindClosestFace(fFaceBoundsCache, p);
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

  const auto normal = TryGetOutwardNormal(closestFaceMatch->face, u, v);
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

G4double G4OCCTSolid::ExactDistanceToIn(const G4ThreeVector& p) const {
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

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p) const { return ExactDistanceToIn(p); }

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

G4double G4OCCTSolid::ExactDistanceToOut(const G4ThreeVector& p) const {
  if (fShape.IsNull()) {
    return 0.0;
  }

  // Query the face compound (not the solid) so OCCT's internal bounding-box
  // tree selects only candidate faces.  A solid-wide query returns distance
  // zero for interior points because the solid contains the vertex; the face
  // compound has no interior volume and gives the correct surface distance.
  BRepExtrema_DistShapeShape distance(MakeVertex(p), fFaceCompound);
  if (!distance.IsDone() || distance.NbSolution() == 0) {
    return 0.0;
  }

  const G4double d = distance.Value();
  return (d <= IntersectionTolerance()) ? 0.0 : d;
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p) const { return ExactDistanceToOut(p); }

G4double G4OCCTSolid::GetCubicVolume() {
  std::unique_lock<std::mutex> lock(fVolumeAreaMutex);
  if (!fCachedVolume) {
    if (fShape.IsNull()) {
      return 0.0;
    }
    GProp_GProps props;
    BRepGProp::VolumeProperties(fShape, props);
    fCachedVolume = props.Mass();
  }
  return *fCachedVolume;
}

G4double G4OCCTSolid::GetSurfaceArea() {
  std::unique_lock<std::mutex> lock(fVolumeAreaMutex);
  if (!fCachedSurfaceArea) {
    if (fShape.IsNull()) {
      return 0.0;
    }
    GProp_GProps props;
    BRepGProp::SurfaceProperties(fShape, props);
    fCachedSurfaceArea = props.Mass();
  }
  return *fCachedSurfaceArea;
}

const G4OCCTSolid::SurfaceSamplingCache& G4OCCTSolid::GetOrBuildSurfaceCache() const {
  // Tessellate first, outside the lock: BRepMesh_IncrementalMesh is idempotent
  // and calling it from multiple threads simultaneously is safe (extra calls
  // after the first are no-ops).
  BRepMesh_IncrementalMesh mesher(fShape, kRelativeDeflection, /*isRelative=*/Standard_True);
  (void)mesher;

  std::unique_lock<std::mutex> lock(fSurfaceCacheMutex);

  const std::uint64_t currentGen = fShapeGeneration.load(std::memory_order_acquire);
  if (fSurfaceCache.has_value() && fSurfaceCacheGeneration == currentGen) {
    return *fSurfaceCache;
  }

  // Build the cache while holding the lock.  Collecting triangle vertices and
  // computing areas is fast (just reading the already-computed triangulation)
  // so blocking other threads briefly is acceptable.
  SurfaceSamplingCache cache;

  for (TopExp_Explorer ex(fShape, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face& face = TopoDS::Face(ex.Current());
    TopLoc_Location loc;
    const Handle(Poly_Triangulation) & triangulation = BRep_Tool::Triangulation(face, loc);
    if (triangulation.IsNull()) {
      continue;
    }

    const gp_Trsf& transform  = loc.Transformation();
    const bool reverseWinding = face.Orientation() == TopAbs_REVERSED;

    for (Standard_Integer i = 1; i <= triangulation->NbTriangles(); ++i) {
      Standard_Integer idx1 = 0;
      Standard_Integer idx2 = 0;
      Standard_Integer idx3 = 0;
      triangulation->Triangle(i).Get(idx1, idx2, idx3);
      if (reverseWinding) {
        std::swap(idx2, idx3);
      }

      const gp_Pnt q1 = triangulation->Node(idx1).Transformed(transform);
      const gp_Pnt q2 = triangulation->Node(idx2).Transformed(transform);
      const gp_Pnt q3 = triangulation->Node(idx3).Transformed(transform);

      const G4ThreeVector v1(q1.X(), q1.Y(), q1.Z());
      const G4ThreeVector v2(q2.X(), q2.Y(), q2.Z());
      const G4ThreeVector v3(q3.X(), q3.Y(), q3.Z());

      const G4double area = 0.5 * (v2 - v1).cross(v3 - v1).mag();
      if (area > 0.0) {
        cache.totalArea += area;
        cache.cumulativeAreas.push_back(cache.totalArea);
        cache.triangles.push_back({v1, v2, v3});
      }
    }
  }

  fSurfaceCache           = std::move(cache);
  fSurfaceCacheGeneration = currentGen;
  return *fSurfaceCache;
}

G4ThreeVector G4OCCTSolid::GetPointOnSurface() const {
  if (fShape.IsNull()) {
    G4ExceptionDescription msg;
    msg << "Shape is null for solid \"" << GetName() << "\".  Returning origin.";
    G4Exception("G4OCCTSolid::GetPointOnSurface", "GeomMgt1001", JustWarning, msg);
    return G4ThreeVector(0.0, 0.0, 0.0);
  }

  const SurfaceSamplingCache& cache = GetOrBuildSurfaceCache();

  if (cache.triangles.empty() || cache.totalArea == 0.0) {
    G4ExceptionDescription msg;
    msg << "Tessellation of solid \"" << GetName()
        << "\" produced no valid triangles.  Returning origin.";
    G4Exception("G4OCCTSolid::GetPointOnSurface", "GeomMgt1001", JustWarning, msg);
    return G4ThreeVector(0.0, 0.0, 0.0);
  }

  // Select a triangle with probability proportional to its area using a
  // binary search on the cumulative-area array.
  const G4double target = G4UniformRand() * cache.totalArea;
  const auto it =
      std::lower_bound(cache.cumulativeAreas.begin(), cache.cumulativeAreas.end(), target);
  const std::size_t idx = std::min(static_cast<std::size_t>(it - cache.cumulativeAreas.begin()),
                                   cache.triangles.size() - 1);
  const SurfaceTriangle& chosen = cache.triangles[idx];

  // Sample uniformly within the chosen triangle using the standard
  // barycentric-coordinate technique: fold the unit square into a triangle
  // by reflecting points with r1+r2 > 1.
  G4double r1 = G4UniformRand();
  G4double r2 = G4UniformRand();
  if (r1 + r2 > 1.0) {
    r1 = 1.0 - r1;
    r2 = 1.0 - r2;
  }

  return chosen.p1 + r1 * (chosen.p2 - chosen.p1) + r2 * (chosen.p3 - chosen.p1);
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

  const auto currentGeneration = fShapeGeneration.load(std::memory_order_acquire);

  {
    std::unique_lock<std::mutex> lock(fPolyhedronMutex);

    // Wait until no other thread is mid-build for a *different* generation, or
    // until the cache is already populated for our generation (fast path).
    fPolyhedronCV.wait(lock, [this, currentGeneration] {
      return !fPolyhedronBuilding || fPolyhedronGeneration == currentGeneration;
    });

    // Cache hit: return an independent copy to the caller.
    if (fCachedPolyhedron && fPolyhedronGeneration == currentGeneration) {
      return new G4Polyhedron(*fCachedPolyhedron);
    }

    // Claim the build slot so other concurrent callers wait for this thread.
    fPolyhedronBuilding = true;
  }

  // ── Tessellation outside the lock ─────────────────────────────────────────
  // Use a relative deflection (1 % of each face's bounding-box size) so that
  // the mesh density scales with the shape rather than being a fixed world-
  // space length that is inappropriate for both very small and very large
  // shapes.
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

  // Finalise the tessellated solid and build a polyhedron from it (still
  // outside the lock — both objects are local to this thread).
  std::unique_ptr<G4Polyhedron> freshPolyhedron;
  if (facetCount > 0) {
    tessellatedSolid.SetSolidClosed(true);
    G4Polyhedron* tmp = tessellatedSolid.GetPolyhedron();
    if (tmp != nullptr) {
      freshPolyhedron = std::make_unique<G4Polyhedron>(*tmp);
    }
  }

  // ── Write cache and wake waiters ──────────────────────────────────────────
  {
    std::unique_lock<std::mutex> lock(fPolyhedronMutex);

    // Only persist the result if the shape has not been replaced while
    // tessellation was running.  Re-reading the generation under the mutex
    // ensures the check and the write are atomic with respect to SetOCCTShape().
    bool cacheWritten = false;
    if (freshPolyhedron && fShapeGeneration.load(std::memory_order_acquire) == currentGeneration) {
      fCachedPolyhedron     = std::make_unique<G4Polyhedron>(*freshPolyhedron);
      fPolyhedronGeneration = currentGeneration;
      cacheWritten          = true;
    }

    // Clear the build slot *after* the cache write so that threads woken by
    // notify_all() see the complete, consistent cache state.
    fPolyhedronBuilding = false;
    fPolyhedronCV.notify_all();

    // Return a copy of the freshly-cached polyhedron when possible; fall back
    // to the locally-built one if the shape was replaced mid-tessellation.
    if (cacheWritten) {
      return new G4Polyhedron(*fCachedPolyhedron);
    }
  }

  return freshPolyhedron ? new G4Polyhedron(*freshPolyhedron) : nullptr;
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
