// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file FaceBoundsSOA.cc
/// @brief Scalar and SIMD implementations of FaceBoundsSOA batch tests.
///
/// This translation unit is compiled by CMake with the flags appropriate for
/// the widest available SIMD ISA (`-mavx2 -mfma`, `-msse4.1`, or none).
/// The preprocessor macros `G4OCCT_HAVE_AVX2` / `G4OCCT_HAVE_SSE4` are set by
/// the compiler when those flags are active and steer the implementation
/// selected by `FaceBoundsSOA`.

#include "G4OCCT/FaceBoundsSOA.hh"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#if defined(G4OCCT_HAVE_AVX2)
#  include <immintrin.h>
#endif

#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>

namespace G4OCCT {

// ── RayZPassFilter ────────────────────────────────────────────────────────────

/// Scalar implementation of the +Z ray AABB filter.
///
/// For a vertical line through (px, py, *) the slab test reduces to a 2-D
/// point-in-rectangle check: the line misses the box iff px is outside
/// [xmin, xmax] or py is outside [ymin, ymax].  The Z slab is always
/// satisfied by an infinite vertical line.
///
/// The inner loop body is branch-free and operates on contiguous aligned
/// arrays; GCC/Clang vectorise it automatically at -O2 -march=native.
void FaceBoundsSOA::RayZPassFilter_scalar(double px, double py, std::uint8_t* out) const {
  const std::size_t n = fPaddedSize;
  // Store raw pointers to help the compiler understand there is no aliasing.
  const double* __restrict__ xmin = fXmin.data();
  const double* __restrict__ xmax = fXmax.data();
  const double* __restrict__ ymin = fYmin.data();
  const double* __restrict__ ymax = fYmax.data();

G4OCCT_IVDEP
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = static_cast<std::uint8_t>((px >= xmin[i]) & (px <= xmax[i]) &
                                       (py >= ymin[i]) & (py <= ymax[i]));
  }
}

void FaceBoundsSOA::RayZPassFilter(double px, double py, std::uint8_t* out) const {
#if defined(G4OCCT_HAVE_AVX2)
  RayZPassFilter_avx2(px, py, out);
#else
  RayZPassFilter_scalar(px, py, out);
#endif
}

// ── RayPassFilter ─────────────────────────────────────────────────────────────

/// Scalar slab test for an infinite line.
///
/// Branches on `dx_zero`, `dy_zero`, `dz_zero` are hoisted outside the loop
/// by the caller, so the inner loop body can be auto-vectorised by the
/// compiler.  `inv_dx/dy/dz` are `1/d` for non-zero components.
void FaceBoundsSOA::RayPassFilter_scalar(double ox, double oy, double oz, double inv_dx,
                                         double inv_dy, double inv_dz, bool dx_zero,
                                         bool dy_zero, bool dz_zero,
                                         std::uint8_t* out) const {
  const std::size_t n        = fPaddedSize;
  const double*     xmin_ptr = fXmin.data();
  const double*     xmax_ptr = fXmax.data();
  const double*     ymin_ptr = fYmin.data();
  const double*     ymax_ptr = fYmax.data();
  const double*     zmin_ptr = fZmin.data();
  const double*     zmax_ptr = fZmax.data();

  constexpr double kInf = std::numeric_limits<double>::infinity();

  // When a direction component is zero the slab test for that axis reduces to
  // a point-in-interval check on the origin; use branch-free arithmetic:
  //   - If origin is inside the slab: tmin=-inf, tmax=+inf (unconstrained)
  //   - If origin is outside the slab: tmin=+inf, tmax=-inf (empty interval)
  // For non-zero components the standard (xmin-o)*inv_d arithmetic applies.

  if (!dx_zero && !dy_zero && !dz_zero) {
    // All axes non-zero: fully vectorisable inner loop.
G4OCCT_IVDEP
    for (std::size_t i = 0; i < n; ++i) {
      double tx1 = (xmin_ptr[i] - ox) * inv_dx;
      double tx2 = (xmax_ptr[i] - ox) * inv_dx;
      if (tx1 > tx2) { double t = tx1; tx1 = tx2; tx2 = t; }

      double ty1 = (ymin_ptr[i] - oy) * inv_dy;
      double ty2 = (ymax_ptr[i] - oy) * inv_dy;
      if (ty1 > ty2) { double t = ty1; ty1 = ty2; ty2 = t; }

      double tz1 = (zmin_ptr[i] - oz) * inv_dz;
      double tz2 = (zmax_ptr[i] - oz) * inv_dz;
      if (tz1 > tz2) { double t = tz1; tz1 = tz2; tz2 = t; }

      const double tmin = std::max({tx1, ty1, tz1});
      const double tmax = std::min({tx2, ty2, tz2});
      out[i]            = static_cast<std::uint8_t>(tmin <= tmax);
    }
    return;
  }

  // General case: at least one axis is zero. Not worth auto-vectorising
  // (degenerate rays are rare in DistanceToIn/Out(p,v)).
  for (std::size_t i = 0; i < fActualSize; ++i) {
    double tmin = -kInf, tmax = kInf;

    // X slab
    if (dx_zero) {
      if (ox < xmin_ptr[i] || ox > xmax_ptr[i]) {
        out[i] = 0;
        continue;
      }
      // tmin/tmax for X unconstrained
    } else {
      double t1 = (xmin_ptr[i] - ox) * inv_dx;
      double t2 = (xmax_ptr[i] - ox) * inv_dx;
      if (t1 > t2) std::swap(t1, t2);
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);
      if (tmin > tmax) { out[i] = 0; continue; }
    }

    // Y slab
    if (dy_zero) {
      if (oy < ymin_ptr[i] || oy > ymax_ptr[i]) {
        out[i] = 0;
        continue;
      }
    } else {
      double t1 = (ymin_ptr[i] - oy) * inv_dy;
      double t2 = (ymax_ptr[i] - oy) * inv_dy;
      if (t1 > t2) std::swap(t1, t2);
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);
      if (tmin > tmax) { out[i] = 0; continue; }
    }

    // Z slab
    if (dz_zero) {
      if (oz < zmin_ptr[i] || oz > zmax_ptr[i]) {
        out[i] = 0;
        continue;
      }
    } else {
      double t1 = (zmin_ptr[i] - oz) * inv_dz;
      double t2 = (zmax_ptr[i] - oz) * inv_dz;
      if (t1 > t2) std::swap(t1, t2);
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);
      if (tmin > tmax) { out[i] = 0; continue; }
    }

    out[i] = 1;
  }
  // Clear any padding elements.
  for (std::size_t i = fActualSize; i < fPaddedSize; ++i) {
    out[i] = 0;
  }
}

void FaceBoundsSOA::RayPassFilter(const gp_Lin& ray, std::uint8_t* out) const {
  const gp_Pnt& loc = ray.Location();
  const gp_Dir& dir = ray.Direction();
  const double  ox  = loc.X(), oy = loc.Y(), oz = loc.Z();
  const double  dx  = dir.X(), dy = dir.Y(), dz = dir.Z();

  constexpr double kEps = 1.0e-12;
  const bool       dx_zero = (std::abs(dx) < kEps);
  const bool       dy_zero = (std::abs(dy) < kEps);
  const bool       dz_zero = (std::abs(dz) < kEps);
  const double     inv_dx  = dx_zero ? 0.0 : 1.0 / dx;
  const double     inv_dy  = dy_zero ? 0.0 : 1.0 / dy;
  const double     inv_dz  = dz_zero ? 0.0 : 1.0 / dz;

#if defined(G4OCCT_HAVE_AVX2)
  RayPassFilter_avx2(ox, oy, oz, inv_dx, inv_dy, inv_dz, dx_zero, dy_zero, dz_zero, out);
#else
  RayPassFilter_scalar(ox, oy, oz, inv_dx, inv_dy, inv_dz, dx_zero, dy_zero, dz_zero, out);
#endif
}

// ── MinPlaneDistance ──────────────────────────────────────────────────────────

/// Scalar minimum plane distance over all faces.
///
/// For each face: dist = |A·px + B·py + C·pz + D|.
/// Non-planar faces have D = kNoPlaneDist, A=B=C=0, so their distance is
/// kNoPlaneDist and they never win.
std::pair<double, std::size_t>
FaceBoundsSOA::MinPlaneDistance_scalar(double px, double py, double pz) const {
  double      minDist = kNoPlaneDist;
  std::size_t minIdx  = fActualSize; // sentinel "not found"

  const double* a_ptr = fPlaneA.data();
  const double* b_ptr = fPlaneB.data();
  const double* c_ptr = fPlaneC.data();
  const double* d_ptr = fPlaneD.data();

  for (std::size_t i = 0; i < fActualSize; ++i) {
    const double val  = a_ptr[i] * px + b_ptr[i] * py + c_ptr[i] * pz + d_ptr[i];
    const double dist = std::abs(val);
    if (dist < minDist) {
      minDist = dist;
      minIdx  = i;
    }
  }
  return {minDist, minIdx};
}

std::pair<double, std::size_t> FaceBoundsSOA::MinPlaneDistance(double px, double py,
                                                                double pz) const {
  if (fActualSize == 0) {
    return {kNoPlaneDist, std::size_t(-1)};
  }
#if defined(G4OCCT_HAVE_AVX2)
  return MinPlaneDistance_avx2(px, py, pz);
#else
  return MinPlaneDistance_scalar(px, py, pz);
#endif
}

// ── AVX2 implementations ──────────────────────────────────────────────────────

#if defined(G4OCCT_HAVE_AVX2)

/// AVX2 +Z ray AABB filter: 4 boxes per iteration.
///
/// The +Z ray test reduces to 2-D point-in-rectangle.  We process 4 boxes
/// per `_mm256_*` instruction and store one byte per face into `out`.
void FaceBoundsSOA::RayZPassFilter_avx2(double px, double py, std::uint8_t* out) const {
  const __m256d px4   = _mm256_set1_pd(px);
  const __m256d py4   = _mm256_set1_pd(py);
  const std::size_t n = fPaddedSize;

  const double* xmin_ptr = fXmin.data();
  const double* xmax_ptr = fXmax.data();
  const double* ymin_ptr = fYmin.data();
  const double* ymax_ptr = fYmax.data();

  for (std::size_t i = 0; i < n; i += kLaneWidth) {
    const __m256d xmin4 = _mm256_load_pd(xmin_ptr + i);
    const __m256d xmax4 = _mm256_load_pd(xmax_ptr + i);
    const __m256d ymin4 = _mm256_load_pd(ymin_ptr + i);
    const __m256d ymax4 = _mm256_load_pd(ymax_ptr + i);

    // pass = (px >= xmin) & (px <= xmax) & (py >= ymin) & (py <= ymax)
    const __m256d x_pass =
        _mm256_and_pd(_mm256_cmp_pd(px4, xmin4, _CMP_GE_OQ),
                      _mm256_cmp_pd(px4, xmax4, _CMP_LE_OQ));
    const __m256d y_pass =
        _mm256_and_pd(_mm256_cmp_pd(py4, ymin4, _CMP_GE_OQ),
                      _mm256_cmp_pd(py4, ymax4, _CMP_LE_OQ));
    const __m256d pass = _mm256_and_pd(x_pass, y_pass);

    // movemask: bit k = sign bit of lane k (1.0 has sign-bit 0; -1 all-bits has sign-bit 1)
    // All-ones double (comparison result "true") has sign-bit 1.
    const int mask = _mm256_movemask_pd(pass);
    out[i + 0]     = static_cast<std::uint8_t>((mask >> 0) & 1);
    out[i + 1]     = static_cast<std::uint8_t>((mask >> 1) & 1);
    out[i + 2]     = static_cast<std::uint8_t>((mask >> 2) & 1);
    out[i + 3]     = static_cast<std::uint8_t>((mask >> 3) & 1);
  }
}

/// AVX2 general-ray slab test: 4 boxes per iteration.
///
/// For the common case where all direction components are non-zero we execute
/// the standard slab test 4-wide.  Degenerate (axis-aligned) rays fall back
/// to the scalar implementation.
void FaceBoundsSOA::RayPassFilter_avx2(double ox, double oy, double oz, double inv_dx,
                                       double inv_dy, double inv_dz, bool dx_zero, bool dy_zero,
                                       bool dz_zero, std::uint8_t* out) const {
  if (dx_zero || dy_zero || dz_zero) {
    // Axis-aligned ray: fall back to the robust scalar implementation.
    RayPassFilter_scalar(ox, oy, oz, inv_dx, inv_dy, inv_dz, dx_zero, dy_zero, dz_zero, out);
    return;
  }

  const __m256d ox4     = _mm256_set1_pd(ox);
  const __m256d oy4     = _mm256_set1_pd(oy);
  const __m256d oz4     = _mm256_set1_pd(oz);
  const __m256d inv_dx4 = _mm256_set1_pd(inv_dx);
  const __m256d inv_dy4 = _mm256_set1_pd(inv_dy);
  const __m256d inv_dz4 = _mm256_set1_pd(inv_dz);

  const double* xmin_ptr = fXmin.data();
  const double* xmax_ptr = fXmax.data();
  const double* ymin_ptr = fYmin.data();
  const double* ymax_ptr = fYmax.data();
  const double* zmin_ptr = fZmin.data();
  const double* zmax_ptr = fZmax.data();

  const std::size_t n = fPaddedSize;

  for (std::size_t i = 0; i < n; i += kLaneWidth) {
    // X slab
    const __m256d tx1 = _mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(xmin_ptr + i), ox4), inv_dx4);
    const __m256d tx2 = _mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(xmax_ptr + i), ox4), inv_dx4);
    __m256d       tmin = _mm256_min_pd(tx1, tx2);
    __m256d       tmax = _mm256_max_pd(tx1, tx2);

    // Y slab
    const __m256d ty1 = _mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(ymin_ptr + i), oy4), inv_dy4);
    const __m256d ty2 = _mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(ymax_ptr + i), oy4), inv_dy4);
    tmin = _mm256_max_pd(tmin, _mm256_min_pd(ty1, ty2));
    tmax = _mm256_min_pd(tmax, _mm256_max_pd(ty1, ty2));

    // Z slab
    const __m256d tz1 = _mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(zmin_ptr + i), oz4), inv_dz4);
    const __m256d tz2 = _mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(zmax_ptr + i), oz4), inv_dz4);
    tmin = _mm256_max_pd(tmin, _mm256_min_pd(tz1, tz2));
    tmax = _mm256_min_pd(tmax, _mm256_max_pd(tz1, tz2));

    // Intersection: tmin <= tmax
    const __m256d pass = _mm256_cmp_pd(tmin, tmax, _CMP_LE_OQ);
    const int     mask = _mm256_movemask_pd(pass);
    out[i + 0]         = static_cast<std::uint8_t>((mask >> 0) & 1);
    out[i + 1]         = static_cast<std::uint8_t>((mask >> 1) & 1);
    out[i + 2]         = static_cast<std::uint8_t>((mask >> 2) & 1);
    out[i + 3]         = static_cast<std::uint8_t>((mask >> 3) & 1);
  }
}

/// AVX2 minimum plane distance: 4-wide FMA computation.
///
/// Computes |A·px + B·py + C·pz + D| for four faces per iteration using
/// FMA instructions, then tracks the running minimum and winning index.
std::pair<double, std::size_t> FaceBoundsSOA::MinPlaneDistance_avx2(double px, double py,
                                                                     double pz) const {
  const __m256d px4 = _mm256_set1_pd(px);
  const __m256d py4 = _mm256_set1_pd(py);
  const __m256d pz4 = _mm256_set1_pd(pz);

  // Bitmask to clear the IEEE 754 sign bit of a double (absolute value).
  const __m256d abs_mask =
      _mm256_castsi256_pd(_mm256_set1_epi64x(static_cast<long long>(0x7FFFFFFFFFFFFFFFL)));

  const __m256d kInf4 = _mm256_set1_pd(kNoPlaneDist);

  const double* a_ptr = fPlaneA.data();
  const double* b_ptr = fPlaneB.data();
  const double* c_ptr = fPlaneC.data();
  const double* d_ptr = fPlaneD.data();

  // Process in groups of 4; track minimum distance and lane-level winning base index.
  __m256d        cur_min4    = kInf4;
  std::size_t    scalar_base = 0; // base index where the scalar minimum lives
  double         scalar_min  = kNoPlaneDist;

  for (std::size_t i = 0; i < fPaddedSize; i += kLaneWidth) {
    const __m256d a4 = _mm256_load_pd(a_ptr + i);
    const __m256d b4 = _mm256_load_pd(b_ptr + i);
    const __m256d c4 = _mm256_load_pd(c_ptr + i);
    const __m256d d4 = _mm256_load_pd(d_ptr + i);

    // dist4 = |a4*px + b4*py + c4*pz + d4|
    __m256d dist4 = _mm256_fmadd_pd(a4, px4, _mm256_fmadd_pd(b4, py4,
                      _mm256_fmadd_pd(c4, pz4, d4)));
    dist4         = _mm256_and_pd(dist4, abs_mask);

    // Check if any lane improved over the running minimum.
    const __m256d improved = _mm256_cmp_pd(dist4, cur_min4, _CMP_LT_OQ);
    if (_mm256_movemask_pd(improved) != 0) {
      // At least one lane improved: fall to scalar to find the exact winner.
      double vals[kLaneWidth];
      _mm256_storeu_pd(vals, dist4);
      for (std::size_t k = 0; k < kLaneWidth; ++k) {
        const std::size_t idx = i + k;
        if (idx < fActualSize && vals[k] < scalar_min) {
          scalar_min  = vals[k];
          scalar_base = idx;
        }
      }
      cur_min4 = _mm256_min_pd(cur_min4, dist4);
    }
  }

  if (scalar_min >= kNoPlaneDist) {
    return {kNoPlaneDist, std::size_t(-1)};
  }
  return {scalar_min, scalar_base};
}

#endif // G4OCCT_HAVE_AVX2

} // namespace G4OCCT
