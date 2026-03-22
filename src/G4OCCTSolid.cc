// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSolid.cc
/// @brief Implementation of G4OCCTSolid.

#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepExtrema_TriangleSet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BVH_Distance.hxx>
#include <BVH_Tools.hxx>
#include <Bnd_Box.hxx>
#include <Extrema_ExtPS.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IntCurvesFace_Intersector.hxx>
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

/// Relative linear deflection used when tessellating the OCCT shape for
/// visualisation (`CreatePolyhedron`) and surface-point sampling
/// (`GetPointOnSurface`).  A value of 0.01 requests a chord height of at
/// most 1 % of each face's bounding-box size.
constexpr Standard_Real kRelativeDeflection = 0.01;

/// BVH traversal class that computes the squared minimum distance from a
/// query point to the nearest triangle in a `BRepExtrema_TriangleSet`.
/// `ComputeDistance()` returns the squared distance; callers must take
/// `std::sqrt` to recover the Euclidean distance.
///
/// Used by `G4OCCTSolid::BVHLowerBoundDistance()` to accelerate safety
/// distance queries.  Follows the pattern of `BRepExtrema_ProximityDistTool`
/// from OCCT's `TKTopAlgo` library.
///
/// Stored distances are squared distances to avoid per-node `std::sqrt`
/// calls; the single square root is taken by the caller after traversal.
class PointToMeshDistance
    : public BVH_Distance<Standard_Real, 3, BVH_Vec3d, BRepExtrema_TriangleSet> {
public:
  /// Prune a BVH branch when its nearest possible point is already farther
  /// than the current best distance.
  Standard_Boolean RejectNode(const BVH_Vec3d& theCornerMin, const BVH_Vec3d& theCornerMax,
                              Standard_Real& theMetric) const override {
    theMetric =
        BVH_Tools<Standard_Real, 3>::PointBoxSquareDistance(myObject, theCornerMin, theCornerMax);
    return RejectMetric(theMetric);
  }

  /// Update the minimum with the squared point-to-triangle distance.
  Standard_Boolean Accept(const Standard_Integer theIndex, const Standard_Real&) override {
    BVH_Vec3d v0, v1, v2;
    myBVHSet->GetVertices(theIndex, v0, v1, v2);
    const Standard_Real sq =
        BVH_Tools<Standard_Real, 3>::PointTriangleSquareDistance(myObject, v0, v1, v2);
    if (sq < myDistance) {
      myDistance = sq;
      return Standard_True;
    }
    return Standard_False;
  }
};

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

/// Evaluate the outward surface normal using a pre-built @p surface adaptor on
/// @p face at parameters (@p u, @p v).
/// Returns the normal vector on success, or std::nullopt if the normal is undefined.
std::optional<G4ThreeVector> TryGetOutwardNormal(const BRepAdaptor_Surface& surface,
                                                 const TopoDS_Face& face, const Standard_Real u,
                                                 const Standard_Real v) {
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

/// Convenience overload: construct the surface adaptor on the fly.
/// Used by rare call sites (e.g. SurfaceNormal, ~84 calls) where no cached
/// adaptor is available; prefer the overload above for hot paths.
std::optional<G4ThreeVector> TryGetOutwardNormal(const TopoDS_Face& face, const Standard_Real u,
                                                 const Standard_Real v) {
  return TryGetOutwardNormal(BRepAdaptor_Surface(face), face, u, v);
}

} // namespace

G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape) {
  if (fShape.IsNull()) {
    throw std::invalid_argument("G4OCCTSolid: shape must not be null");
  }
  ComputeBounds();
}

G4OCCTSolid::~G4OCCTSolid() {
  // Manually clear cached OCCT objects to avoid G4Exception during static destruction.
  // G4Cache::Destroy() calls G4Exception with FatalException if cache size validation
  // fails, which can happen when G4SolidStore cleanup occurs during exit() and the
  // thread-local storage has already been torn down.
  // Resetting the optional values here releases the OCCT objects before G4Cache
  // attempts to destroy the thread-local storage.
  fClassifierCache.Get().classifier.reset();
  fIntersectorCache.Get().faceIntersectors.clear();
  fSphereCache.Get().spheres.clear();
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

  Bnd_Box boundingBox;
  BRepBndLib::AddOptimal(fShape, boundingBox, /*useTriangulation=*/Standard_False);
  if (boundingBox.IsVoid()) {
    throw std::invalid_argument("G4OCCTSolid: shape has no computable bounding box (no geometry)");
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
  fFaceAdaptorIndex.clear();
  G4double maxFaceDiag = 0.0;
  for (TopExp_Explorer ex(fShape, TopAbs_FACE); ex.More(); ex.Next()) {
    Bnd_Box faceBox;
    BRepBndLib::AddOptimal(ex.Current(), faceBox, /*useTriangulation=*/Standard_False);
    const TopoDS_Face& currentFace = TopoDS::Face(ex.Current());
    const std::size_t idx          = fFaceBoundsCache.size();
    fFaceBoundsCache.push_back({currentFace, faceBox, BRepAdaptor_Surface(currentFace)});
    fFaceAdaptorIndex[currentFace.TShape().get()].push_back(idx);
    // Track the largest face bounding-box diagonal to bound the tessellation error.
    if (!faceBox.IsVoid()) {
      Standard_Real fx0 = 0.0;
      Standard_Real fy0 = 0.0;
      Standard_Real fz0 = 0.0;
      Standard_Real fx1 = 0.0;
      Standard_Real fy1 = 0.0;
      Standard_Real fz1 = 0.0;
      faceBox.Get(fx0, fy0, fz0, fx1, fy1, fz1);
      const G4double diag = G4ThreeVector(fx1 - fx0, fy1 - fy0, fz1 - fz0).mag();
      maxFaceDiag         = std::max(maxFaceDiag, diag);
    }
  }

  // fBVHDeflection bounds the Hausdorff distance between the analytical surface
  // and the tessellation built with relative deflection kRelativeDeflection.
  fBVHDeflection = kRelativeDeflection * maxFaceDiag;

  // Ensure a triangulation is present (idempotent if mesh already exists).
  // Limit lifetime to this scope so mesh resources are released before building
  // the BVH, preserving the original temporary-object behavior.
  {
    [[maybe_unused]] const BRepMesh_IncrementalMesh mesher(fShape, kRelativeDeflection,
                                                           /*isRelative=*/Standard_True);
  }

  // Build the BVH-accelerated triangle set over all tessellated faces.
  BRepExtrema_ShapeList faces;
  for (TopExp_Explorer ex(fShape, TopAbs_FACE); ex.More(); ex.Next()) {
    faces.Append(ex.Current());
  }
  if (faces.IsEmpty()) {
    fTriangleSet.Nullify();
  } else {
    fTriangleSet = new BRepExtrema_TriangleSet(faces);
  }

  ComputeInitialSpheres();
}

void G4OCCTSolid::ComputeInitialSpheres() {
  fInitialSpheres.clear();

  const G4double tol          = IntersectionTolerance();
  const G4ThreeVector& bmin   = fCachedBounds.min;
  const G4ThreeVector& bmax   = fCachedBounds.max;
  const G4ThreeVector centre  = 0.5 * (bmin + bmax);
  const G4ThreeVector halfExt = 0.5 * (bmax - bmin);

  // Candidate seed points: AABB centre, 6 axis-offset interior points (each at half
  // the distance from the AABB centre to the nearest face along that axis), and 8 octant
  // centres (15 total).
  std::vector<G4ThreeVector> candidates;
  candidates.reserve(15);
  candidates.push_back(centre);
  for (const G4double s : {-0.5, 0.5}) {
    candidates.push_back(centre + G4ThreeVector(s * halfExt.x(), 0.0, 0.0));
    candidates.push_back(centre + G4ThreeVector(0.0, s * halfExt.y(), 0.0));
    candidates.push_back(centre + G4ThreeVector(0.0, 0.0, s * halfExt.z()));
  }
  for (const int sx : {-1, 1}) {
    for (const int sy : {-1, 1}) {
      for (const int sz : {-1, 1}) {
        candidates.push_back(centre + G4ThreeVector(0.75 * sx * halfExt.x(),
                                                    0.75 * sy * halfExt.y(),
                                                    0.75 * sz * halfExt.z()));
      }
    }
  }

  // Use a local classifier: construction-time only, no thread-local needed.
  BRepClass3d_SolidClassifier localClassifier;
  localClassifier.Load(fShape);

  for (const G4ThreeVector& cand : candidates) {
    localClassifier.Perform(ToPoint(cand), tol);
    if (localClassifier.State() != TopAbs_IN) {
      continue;
    }
    G4double d = BVHLowerBoundDistance(cand);
    if (d >= kInfinity || d <= tol) {
      // BVH unavailable or too close to surface; try exact distance.
      const auto match = TryFindClosestFace(fFaceBoundsCache, cand);
      if (!match.has_value() || match->distance <= tol) {
        continue;
      }
      d = match->distance;
    }
    fInitialSpheres.push_back({cand, d});
  }
  // Sort descending by radius so the most useful spheres are checked first.
  std::sort(fInitialSpheres.begin(), fInitialSpheres.end(),
            [](const InscribedSphere& a, const InscribedSphere& b) { return a.radius > b.radius; });
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

    // Use Extrema_ExtPS for a lighter-weight point-to-surface distance that
    // avoids the NCollection_Sequence allocations of BRepExtrema_DistShapeShape.
    const Standard_Real tol = Precision::Confusion();
    Extrema_ExtPS ext(queryPoint, fb.adaptor, tol, tol);
    G4double candidateDistance = kInfinity;
    if (ext.IsDone() && ext.NbExt() > 0) {
      for (Standard_Integer k = 1; k <= ext.NbExt(); ++k) {
        candidateDistance = std::min(candidateDistance, std::sqrt(ext.SquareDistance(k)));
      }
    } else {
      // Fallback: Extrema_ExtPS found no solutions (e.g. degenerate surface).
      BRepExtrema_DistShapeShape distance(queryVertex, fb.face);
      if (!distance.IsDone() || distance.NbSolution() == 0) {
        continue;
      }
      candidateDistance = distance.Value();
    }

    if (bestMatch.has_value() && candidateDistance >= bestMatch->distance) {
      continue;
    }
    bestMatch = ClosestFaceMatch{.face = fb.face, .distance = candidateDistance};
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

G4OCCTSolid::IntersectorCache& G4OCCTSolid::GetOrCreateIntersector() const {
  IntersectorCache& cache        = fIntersectorCache.Get();
  const std::uint64_t currentGen = fShapeGeneration.load(std::memory_order_acquire);
  if (cache.generation != currentGen) {
    const G4double tol = IntersectionTolerance();
    cache.faceIntersectors.clear();
    cache.faceIntersectors.reserve(fFaceBoundsCache.size());
    cache.expandedBoxes.clear();
    cache.expandedBoxes.reserve(fFaceBoundsCache.size());
    for (const auto& fb : fFaceBoundsCache) {
      cache.faceIntersectors.push_back(
          std::make_unique<IntCurvesFace_Intersector>(fb.face, tol)); // O(1) per face
      Bnd_Box expanded = fb.box;
      expanded.Enlarge(tol); // prevent false-positive IsOut for zero-thickness planar face boxes
      cache.expandedBoxes.push_back(std::move(expanded));
    }
    cache.generation = currentGen;
  }
  return cache;
}

G4OCCTSolid::SphereCacheData& G4OCCTSolid::GetOrInitSphereCache() const {
  SphereCacheData& cache         = fSphereCache.Get();
  const std::uint64_t currentGen = fShapeGeneration.load(std::memory_order_acquire);
  if (cache.generation != currentGen) {
    // Re-seed from the construction-time initial spheres (already sorted descending).
    cache.spheres    = fInitialSpheres;
    cache.generation = currentGen;
  }
  return cache;
}

void G4OCCTSolid::TryInsertSphere(const G4ThreeVector& centre, G4double d) const {
  if (d <= 0.0) {
    return;
  }

  // Enforce a minimum meaningful radius, consistent with ComputeInitialSpheres().
  const G4double minRadius = IntersectionTolerance();
  if (d <= minRadius) {
    return;
  }
  SphereCacheData& cache = GetOrInitSphereCache();

  // Quick capacity check: if at capacity and the new radius is no better than
  // the smallest stored radius, there is nothing to gain.
  if (cache.spheres.size() >= kMaxInscribedSpheres && d <= cache.spheres.back().radius) {
    return;
  }

  // Dominance check: skip if the new sphere is fully contained inside an existing one.
  // Condition: |centre − cᵢ| + d ≤ rᵢ  ↔  |centre − cᵢ|² ≤ (rᵢ − d)² (when rᵢ ≥ d).
  for (const InscribedSphere& s : cache.spheres) {
    if (s.radius >= d) {
      const G4double gap = s.radius - d;
      if ((centre - s.centre).mag2() <= gap * gap) {
        return;
      }
    }
  }

  // Insert in sorted position (descending by radius).
  const InscribedSphere newSphere{centre, d};
  const auto it = std::lower_bound(
      cache.spheres.begin(), cache.spheres.end(), newSphere,
      [](const InscribedSphere& a, const InscribedSphere& b) { return a.radius > b.radius; });
  cache.spheres.insert(it, newSphere);

  // Evict the smallest sphere if the cache has grown beyond capacity.
  if (cache.spheres.size() > kMaxInscribedSpheres) {
    cache.spheres.pop_back();
  }
}

// ── G4VSolid pure-virtual implementations ────────────────────────────────────

EInside G4OCCTSolid::Inside(const G4ThreeVector& p) const {
  const G4double tolerance = IntersectionTolerance();
  if (p.x() < fCachedBounds.min.x() - tolerance || p.x() > fCachedBounds.max.x() + tolerance ||
      p.y() < fCachedBounds.min.y() - tolerance || p.y() > fCachedBounds.max.y() + tolerance ||
      p.z() < fCachedBounds.min.z() - tolerance || p.z() > fCachedBounds.max.z() + tolerance) {
    return kOutside;
  }

  // Fast inscribed-sphere check: if p lies strictly inside any cached sphere
  // (with a tolerance margin), every such sphere is provably interior to the
  // solid, so we can return kInside immediately without an OCCT classifier call.
  // The check uses (radius - tolerance) to avoid misclassifying boundary-adjacent
  // points as kInside — matching the open-ball guarantee from the safety distance.
  const SphereCacheData& sphereCache = GetOrInitSphereCache();
  for (const InscribedSphere& s : sphereCache.spheres) {
    const G4double interiorRadius = s.radius - tolerance;
    if (interiorRadius > 0.0 && (p - s.centre).mag2() < interiorRadius * interiorRadius) {
      return kInside;
    }
  }

  // Tier-2: ray-parity test using per-thread intersector cache.
  // Cast a ray from p in the canonical +Z direction and count how many faces
  // the ray crosses strictly through their interior (TopAbs_IN).  Odd = inside.
  // TopAbs_ON hits (at face edges/vertices) are skipped to avoid double-counting
  // shared edges; if no clean crossings are found we fall back to the classifier.
  IntersectorCache& cache = GetOrCreateIntersector();
  const gp_Lin ray(ToPoint(p), gp_Dir(0.0, 0.0, 1.0));
  int crossings  = 0;
  bool onSurface = false;

  for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
    if (cache.expandedBoxes[i].IsOut(ray)) {
      continue;
    }
    IntCurvesFace_Intersector& fi = *cache.faceIntersectors[i];
    fi.Perform(ray, -tolerance, Precision::Infinite());
    if (!fi.IsDone()) {
      continue;
    }
    for (Standard_Integer j = 1; j <= fi.NbPnt(); ++j) {
      const G4double w = fi.WParameter(j);
      if (std::abs(w) <= tolerance) {
        onSurface = true;
      } else if (w > tolerance && fi.State(j) == TopAbs_IN) {
        ++crossings;
      }
    }
  }

  if (onSurface) {
    return kSurface;
  }
  // Safety fallback for degenerate rays (e.g. through edge/vertex giving 0
  // TopAbs_IN hits): if we're inside the AABB but got zero crossings, the
  // classifier handles these corner cases correctly.
  if (crossings == 0) {
    BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();
    classifier.Perform(ToPoint(p), tolerance);
    return ToG4Inside(classifier.State());
  }
  return (crossings % 2 == 1) ? kInside : kOutside;
}

G4ThreeVector G4OCCTSolid::SurfaceNormal(const G4ThreeVector& p) const {
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
  const G4double tolerance = IntersectionTolerance();
  const gp_Lin ray(ToPoint(p), gp_Dir(v.x(), v.y(), v.z()));

  IntersectorCache& cache = GetOrCreateIntersector();

  G4double minDistance = kInfinity;
  for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
    if (cache.expandedBoxes[i].IsOut(ray)) {
      continue;
    }
    IntCurvesFace_Intersector& fi = *cache.faceIntersectors[i];
    fi.Perform(ray, tolerance, Precision::Infinite());
    if (!fi.IsDone()) {
      continue;
    }
    for (Standard_Integer j = 1; j <= fi.NbPnt(); ++j) {
      const G4double w = fi.WParameter(j);
      if (w > tolerance && w < minDistance) {
        minDistance = w;
      }
    }
  }

  return minDistance;
}

G4double G4OCCTSolid::ExactDistanceToIn(const G4ThreeVector& p) const {
  BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();
  classifier.Perform(ToPoint(p), IntersectionTolerance());
  if (classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON) {
    return 0.0;
  }

  const auto match = TryFindClosestFace(fFaceBoundsCache, p);
  if (!match.has_value()) {
    return kInfinity;
  }
  return (match->distance <= IntersectionTolerance()) ? 0.0 : match->distance;
}

G4double G4OCCTSolid::AABBLowerBound(const G4ThreeVector& p) const {
  const G4ThreeVector& mn = fCachedBounds.min;
  const G4ThreeVector& mx = fCachedBounds.max;
  const G4double dx       = std::max({0.0, mn.x() - p.x(), p.x() - mx.x()});
  const G4double dy       = std::max({0.0, mn.y() - p.y(), p.y() - mx.y()});
  const G4double dz       = std::max({0.0, mn.z() - p.z(), p.z() - mx.z()});
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

G4double G4OCCTSolid::BVHLowerBoundDistance(const G4ThreeVector& p) const {
  if (fTriangleSet.IsNull() || fTriangleSet->Size() == 0) {
    return kInfinity;
  }
  PointToMeshDistance solver;
  solver.SetObject(BVH_Vec3d(p.x(), p.y(), p.z()));
  solver.SetBVHSet(fTriangleSet.get());
  const Standard_Real meshDistSq = solver.ComputeDistance();
  if (!solver.IsDone()) {
    return kInfinity;
  }
  // The solver returns a squared distance; take the single sqrt here
  // before subtracting the deflection bound (which is in actual-distance space).
  const G4double meshDist = std::sqrt(static_cast<G4double>(meshDistSq));
  return std::max(0.0, meshDist - fBVHDeflection);
}

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p) const {
  // Tier-0: AABB lower bound (O(1)).  If the point is clearly outside the shape's
  // axis-aligned bounding box, the AABB distance is a guaranteed conservative lower
  // bound on the true surface distance and avoids any further OCCT computation.
  const G4double aabbDist = AABBLowerBound(p);
  if (aabbDist > IntersectionTolerance()) {
    return aabbDist;
  }

  // Fallback: exact distance (handles points near or inside the AABB, including
  // interior/surface classification).
  return ExactDistanceToIn(p);
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v,
                                    const G4bool calcNorm, G4bool* validNorm,
                                    G4ThreeVector* n) const {
  if (validNorm != nullptr) {
    *validNorm = false;
  }

  const G4double tolerance = IntersectionTolerance();
  const gp_Lin ray(ToPoint(p), gp_Dir(v.x(), v.y(), v.z()));

  IntersectorCache& cache = GetOrCreateIntersector();

  G4double minDistance   = kInfinity;
  std::size_t minFaceIdx = std::numeric_limits<std::size_t>::max();
  G4double minU          = 0.0;
  G4double minV          = 0.0;
  bool minIsIn           = false;

  for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
    if (cache.expandedBoxes[i].IsOut(ray)) {
      continue;
    }
    IntCurvesFace_Intersector& fi = *cache.faceIntersectors[i];
    fi.Perform(ray, tolerance, Precision::Infinite());
    if (!fi.IsDone()) {
      continue;
    }
    for (Standard_Integer j = 1; j <= fi.NbPnt(); ++j) {
      const G4double w = fi.WParameter(j);
      if (w > tolerance && w < minDistance) {
        minDistance = w;
        minFaceIdx  = i;
        minU        = fi.UParameter(j);
        minV        = fi.VParameter(j);
        minIsIn     = (fi.State(j) == TopAbs_IN || fi.State(j) == TopAbs_ON);
      }
    }
  }

  if (minFaceIdx == std::numeric_limits<std::size_t>::max() || minDistance == kInfinity) {
    return 0.0;
  }

  if (calcNorm && validNorm != nullptr && n != nullptr && minIsIn) {
    const FaceBounds& fb = fFaceBoundsCache[minFaceIdx];
    const auto outNorm   = TryGetOutwardNormal(fb.adaptor, fb.face, minU, minV);
    if (outNorm) {
      *n         = *outNorm;
      *validNorm = true;
    }
  }

  return minDistance;
}

G4double G4OCCTSolid::ExactDistanceToOut(const G4ThreeVector& p) const {
  // Use the pre-built per-face AABB cache to prune candidates before calling
  // BRepExtrema on individual faces: each query performs O(N_faces) cheap AABB
  // checks, but only O(k) faces require expensive extrema evaluations.
  const auto match = TryFindClosestFace(fFaceBoundsCache, p);
  if (!match.has_value()) {
    return 0.0;
  }
  return (match->distance <= IntersectionTolerance()) ? 0.0 : match->distance;
}

G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p) const {
  // Tier-1: mesh-BVH lower bound (O(log T)).  For interior points the AABB
  // distance is always 0, so there is no useful Tier-0 AABB shortcut here.
  const G4double bvhDist = BVHLowerBoundDistance(p);
  const G4double d       = (bvhDist < kInfinity) ? bvhDist : ExactDistanceToOut(p);
  // Feed the inscribed-sphere cache: every positive return value proves B(p,d)
  // is inside the solid and can accelerate future Inside(p) calls.
  TryInsertSphere(p, d);
  return d;
}

G4double G4OCCTSolid::GetCubicVolume() {
  std::unique_lock<std::mutex> lock(fVolumeAreaMutex);
  if (!fCachedVolume) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(fShape, props);
    fCachedVolume = props.Mass();
  }
  return *fCachedVolume;
}

G4double G4OCCTSolid::GetSurfaceArea() {
  std::unique_lock<std::mutex> lock(fVolumeAreaMutex);
  if (!fCachedSurfaceArea) {
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

    // Register this face once in the deduplicated faces array.
    const auto faceIndex = static_cast<std::uint32_t>(cache.faces.size());
    cache.faces.push_back(face);

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
        cache.triangles.push_back({v1, v2, v3, faceIndex});
      }
    }
  }

  fSurfaceCache           = std::move(cache);
  fSurfaceCacheGeneration = currentGen;
  return *fSurfaceCache;
}

G4ThreeVector G4OCCTSolid::GetPointOnSurface() const {
  const SurfaceSamplingCache& cache = GetOrBuildSurfaceCache();

  if (cache.triangles.empty() || cache.totalArea == 0.0) {
    G4ExceptionDescription msg;
    msg << "Tessellation of solid \"" << GetName()
        << "\" produced no valid triangles.  Returning origin.";
    G4Exception("G4OCCTSolid::GetPointOnSurface", "GeomMgt1001", JustWarning, msg);
    return {0.0, 0.0, 0.0};
  }

  // Select a triangle with probability proportional to its area using a
  // binary search on the cumulative-area array.
  const G4double target = G4UniformRand() * cache.totalArea;
  const auto it         = std::ranges::lower_bound(cache.cumulativeAreas, target);
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

  const G4ThreeVector tessPoint =
      chosen.p1 + r1 * (chosen.p2 - chosen.p1) + r2 * (chosen.p3 - chosen.p1);

  // For curved faces the tessellation triangle is a planar chord that lies
  // inside the solid.  Project the sampled point back to the nearest point on
  // the analytical face surface so that the returned point truly lies on the
  // boundary and passes the G4PVPlacement::CheckOverlaps() surface test.
  const TopoDS_Face& face = cache.faces[chosen.faceIndex];
  TopLoc_Location loc;
  const Handle(Geom_Surface) geomSurface = BRep_Tool::Surface(face, loc);
  if (!geomSurface.IsNull()) {
    gp_Pnt tessPointLocal(tessPoint.x(), tessPoint.y(), tessPoint.z());
    if (!loc.IsIdentity()) {
      tessPointLocal.Transform(loc.Transformation().Inverted());
    }
    GeomAPI_ProjectPointOnSurf projection(tessPointLocal, geomSurface);
    if (projection.NbPoints() > 0) {
      gp_Pnt projectedPoint = projection.NearestPoint();
      if (!loc.IsIdentity()) {
        projectedPoint.Transform(loc.Transformation());
      }
      return {projectedPoint.X(), projectedPoint.Y(), projectedPoint.Z()};
    }
  }

  return tessPoint;
}

G4GeometryType G4OCCTSolid::GetEntityType() const { return "G4OCCTSolid"; }

G4VisExtent G4OCCTSolid::GetExtent() const {
  return {fCachedBounds.min.x(), fCachedBounds.max.x(), fCachedBounds.min.y(),
          fCachedBounds.max.y(), fCachedBounds.min.z(), fCachedBounds.max.z()};
}

void G4OCCTSolid::BoundingLimits(G4ThreeVector& pMin, G4ThreeVector& pMax) const {
  pMin = fCachedBounds.min;
  pMax = fCachedBounds.max;
}

G4bool G4OCCTSolid::CalculateExtent(const EAxis pAxis, const G4VoxelLimits& pVoxelLimit,
                                    const G4AffineTransform& pTransform, G4double& pMin,
                                    G4double& pMax) const {
  const G4BoundingEnvelope envelope(fCachedBounds.min, fCachedBounds.max);
  return envelope.CalculateExtent(pAxis, pVoxelLimit, G4Transform3D(pTransform), pMin, pMax);
}

void G4OCCTSolid::DescribeYourselfTo(G4VGraphicsScene& scene) const { scene.AddSolid(*this); }

G4Polyhedron* G4OCCTSolid::CreatePolyhedron() const {
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
