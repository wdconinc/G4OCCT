// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT/SimdSupport.hh
/// @brief Compile-time ISA detection and SIMD portability helpers.
///
/// Algorithm code should include this header to access:
/// - Detection macros (`GOCCT_HAVE_AVX2`, `GOCCT_HAVE_SSE4`, `GOCCT_HAVE_FMA`)
/// - `G4OCCT::AlignedAllocator<T, Alignment>` for SIMD-aligned `std::vector` storage
///
/// No SIMD intrinsics appear here; they live in implementation files that are
/// compiled with the appropriate `-mavx2`/`-msse4.1` flags.

#pragma once

#include <cstddef>
#include <new>

// ── Compile-time ISA detection ────────────────────────────────────────────────

#if defined(__AVX2__)
/// Defined when the translation unit is compiled with AVX2 support.
#  define GOCCT_HAVE_AVX2 1
#endif

#if defined(__SSE4_1__)
/// Defined when the translation unit is compiled with SSE 4.1 support.
#  define GOCCT_HAVE_SSE4 1
#endif

#if defined(__FMA__)
/// Defined when the translation unit is compiled with FMA support.
#  define GOCCT_HAVE_FMA 1
#endif

// ── Aligned allocator ─────────────────────────────────────────────────────────

namespace G4OCCT {

/// Minimal aligned allocator for use with `std::vector`.
///
/// Ensures allocated arrays start on an `Alignment`-byte boundary so that
/// aligned SIMD loads (e.g. `_mm256_load_pd`) operate without fault.
/// The default alignment of 32 bytes suits both SSE (16-byte) and AVX2
/// (32-byte) loads.
template <typename T, std::size_t Alignment = 32>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;

  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }
    void* ptr = ::operator new(n * sizeof(T), std::align_val_t{Alignment});
    return static_cast<T*>(ptr);
  }

  void deallocate(T* ptr, std::size_t) noexcept {
    ::operator delete(ptr, std::align_val_t{Alignment});
  }

  template <typename U>
  struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };

  bool operator==(const AlignedAllocator&) const noexcept { return true; }
  bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

} // namespace G4OCCT
