// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT/FaceBoundsSOA.hh
/// @brief Struct-of-Arrays (SoA) layout for per-face bounding-box and plane
///        data, with SIMD-accelerated batch geometric tests.
///
/// `FaceBoundsSOA` stores the same bounding-box and plane data that is held
/// in `G4OCCTSolid::fFaceBoundsCache`, but in a layout that exposes contiguous
/// arrays for each coordinate component.  This enables the compiler (and
/// explicit AVX2 intrinsics) to process multiple faces per clock cycle.
///
/// **Algorithm code does not need to know about SIMD.**  Call sites use the
/// three high-level methods:
///   - `RayZPassFilter(px, py, out)`   — prefilter for the canonical +Z ray
///     used in `Inside(p)`.
///   - `RayPassFilter(ray, out)`       — prefilter for general rays used in
///     `DistanceToIn(p,v)` and `DistanceToOut(p,v)`.
///   - `MinPlaneDistance(px, py, pz)`  — minimum plane distance over all planar
///     faces, used in `SurfaceNormal(p)` and `PlanarFaceLowerBoundDistance`.
///
/// The SIMD implementation is selected at compile time by the macros defined
/// in `SimdSupport.hh` (`GOCCT_HAVE_AVX2`, `GOCCT_HAVE_SSE4`).  On platforms
/// without any SIMD support the scalar fallback is used instead; it is written
/// in a form that GCC/Clang can auto-vectorize with `-O3 -march=native`.

#pragma once

#include "G4OCCT/SimdSupport.hh"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gp_Lin.hxx>

namespace G4OCCT {

/// Struct-of-Arrays bounding-box store with SIMD batch query methods.
///
/// Populated once per shape update by `G4OCCTSolid::ComputeBounds()` and
/// then shared read-only across all threads.  The arrays are padded to a
/// multiple of `kLaneWidth` (4 for AVX2 double) so that SIMD loops never
/// need boundary guards.
class FaceBoundsSOA {
public:
  /// Number of doubles processed per AVX2 loop iteration.
  static constexpr std::size_t kLaneWidth = 4;

  /// Sentinel distance used for non-planar faces in the plane-distance arrays.
  static constexpr double kNoPlaneDist = 1.0e300;

  /// Populate SoA arrays from a per-face cache.
  ///
  /// @tparam FaceCache  Range whose elements expose `box` (`Bnd_Box`),
  ///                    `plane` (`std::optional<gp_Pln>`).
  /// @param  cache      Per-face metadata vector from `G4OCCTSolid`.
  /// @param  tolerance  Enlargement applied to each face box (same value
  ///                    used by `IntersectorCache::expandedBoxes`).
  template <typename FaceCache>
  void Build(const FaceCache& cache, double tolerance);

  /// Prefilter for the canonical +Z ray used inside `Inside(p)`.
  ///
  /// For an infinite line through `(px, py, *)` in the +Z direction the
  /// AABB test reduces to a 2-D point-in-rectangle check: the line misses
  /// the box iff `px` is outside `[xmin, xmax]` or `py` is outside
  /// `[ymin, ymax]`.  Setting `out[i] = 1` means face `i` should be tested.
  ///
  /// @param px, py  XY coordinates of the query point.
  /// @param out     Caller-allocated array of at least `PaddedSize()` bytes.
  void RayZPassFilter(double px, double py, std::uint8_t* out) const;

  /// Prefilter for a general ray (used in `DistanceToIn/Out(p,v)`).
  ///
  /// Performs an AABB slab test against the infinite line.
  /// Setting `out[i] = 1` means face `i` should be tested.
  ///
  /// @param ray  OCCT line (origin + direction).
  /// @param out  Caller-allocated array of at least `PaddedSize()` bytes.
  void RayPassFilter(const gp_Lin& ray, std::uint8_t* out) const;

  /// Return the minimum plane distance and the index of the closest planar face.
  ///
  /// Non-planar faces contribute `kNoPlaneDist` and are never the minimum.
  /// Returns `{kNoPlaneDist, npos}` when `Size() == 0`.
  ///
  /// @param px, py, pz  Query point coordinates.
  std::pair<double, std::size_t> MinPlaneDistance(double px, double py, double pz) const;

  /// Number of faces stored (before padding).
  std::size_t Size() const { return fActualSize; }

  /// Size of the internal arrays (rounded up to `kLaneWidth`).
  ///
  /// Callers must allocate **at least** `PaddedSize()` bytes for the `out`
  /// buffers passed to `RayZPassFilter` and `RayPassFilter`.
  std::size_t PaddedSize() const { return fPaddedSize; }

private:
  using AlignedVec = std::vector<double, AlignedAllocator<double, 32>>;

  /// Populate `out[0..fPaddedSize)` with a scalar +Z ray AABB filter.
  void RayZPassFilter_scalar(double px, double py, std::uint8_t* out) const;
  /// Populate `out[0..fPaddedSize)` with a scalar general-ray AABB slab filter.
  void RayPassFilter_scalar(double ox, double oy, double oz, double inv_dx, double inv_dy,
                            double inv_dz, bool dx_zero, bool dy_zero, bool dz_zero,
                            std::uint8_t* out) const;
  /// Return minimum plane distance over `[0, fPaddedSize)` using scalar code.
  std::pair<double, std::size_t> MinPlaneDistance_scalar(double px, double py, double pz) const;

#if defined(GOCCT_HAVE_AVX2)
  void             RayZPassFilter_avx2(double px, double py, std::uint8_t* out) const;
  void             RayPassFilter_avx2(double ox, double oy, double oz, double inv_dx,
                                      double inv_dy, double inv_dz, bool dx_zero, bool dy_zero,
                                      bool dz_zero, std::uint8_t* out) const;
  std::pair<double, std::size_t> MinPlaneDistance_avx2(double px, double py, double pz) const;
#endif

  // Box corners (tolerance-enlarged), aligned, padded to kLaneWidth.
  AlignedVec fXmin, fXmax;
  AlignedVec fYmin, fYmax;
  AlignedVec fZmin, fZmax;

  // Plane coefficients for each face.  For non-planar faces:
  //   fPlaneA[i] = fPlaneB[i] = fPlaneC[i] = 0.0, fPlaneD[i] = kNoPlaneDist.
  // For planar faces: the plane equation is  A·x + B·y + C·z + D = 0,
  // with (A,B,C) a unit normal and D = -n · (point on plane).
  AlignedVec fPlaneA, fPlaneB, fPlaneC, fPlaneD;

  std::size_t fActualSize{0}; ///< True number of faces.
  std::size_t fPaddedSize{0}; ///< fActualSize rounded up to kLaneWidth.
};

} // namespace G4OCCT

// ── Template implementation ───────────────────────────────────────────────────

#include <Bnd_Box.hxx>
#include <gp_Pln.hxx>

template <typename FaceCache>
void G4OCCT::FaceBoundsSOA::Build(const FaceCache& cache, double tolerance) {
  fActualSize = cache.size();
  // Pad to a multiple of kLaneWidth so SIMD loops need no tail handling.
  fPaddedSize = ((fActualSize + kLaneWidth - 1) / kLaneWidth) * kLaneWidth;

  auto resize = [&](AlignedVec& v, double fill) {
    v.assign(fPaddedSize, fill);
  };

  resize(fXmin, 1.0e300);
  resize(fXmax, -1.0e300);
  resize(fYmin, 1.0e300);
  resize(fYmax, -1.0e300);
  resize(fZmin, 1.0e300);
  resize(fZmax, -1.0e300);
  resize(fPlaneA, 0.0);
  resize(fPlaneB, 0.0);
  resize(fPlaneC, 0.0);
  resize(fPlaneD, kNoPlaneDist);

  for (std::size_t i = 0; i < fActualSize; ++i) {
    const auto& fb = cache[i];

    // Expand the face box by the intersection tolerance (same as IntersectorCache).
    Bnd_Box expanded = fb.box;
    expanded.Enlarge(tolerance);

    if (!expanded.IsVoid()) {
      double x0, y0, z0, x1, y1, z1;
      expanded.Get(x0, y0, z0, x1, y1, z1);
      fXmin[i] = x0;
      fXmax[i] = x1;
      fYmin[i] = y0;
      fYmax[i] = y1;
      fZmin[i] = z0;
      fZmax[i] = z1;
    }

    if (fb.plane.has_value()) {
      const gp_Pln& pln = *fb.plane;
      // gp_Pln stores the plane as a coordinate system; the plane equation is
      //   A(x-x0) + B(y-y0) + C(z-z0) = 0,  i.e.  A·x + B·y + C·z + D = 0
      // with (A,B,C) = plane normal (unit) and D = -A·x0 - B·y0 - C·z0.
      double A, B, C, D;
      pln.Coefficients(A, B, C, D);
      fPlaneA[i] = A;
      fPlaneB[i] = B;
      fPlaneC[i] = C;
      fPlaneD[i] = D;
    }
  }
}
