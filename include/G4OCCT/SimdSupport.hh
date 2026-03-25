// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT/SimdSupport.hh
/// @brief SIMD portability helpers: target-attribute macros, runtime CPU checks,
///        auto-vectorisation hints, and the aligned allocator.
///
/// Algorithm code includes this header to access:
/// - `G4OCCT_TARGET_AVX2` / `G4OCCT_TARGET_DEFAULT` — GCC/Clang function
///   target attributes that compile specific functions for a given ISA without
///   requiring global `-mavx2` flags.
/// - `G4OCCT_CPU_HAS_AVX2` — runtime CPU capability check via
///   `__builtin_cpu_supports`; evaluates to `false` when `G4OCCT_USE_SIMD`
///   is not defined or the compiler lacks the builtin.
/// - `G4OCCT::AlignedAllocator<T, Alignment>` for SIMD-aligned `std::vector`.
///
/// No SIMD intrinsics appear here; they live in `FaceBoundsSOA.cc` which uses
/// `__attribute__((target(...)))` to compile ISA-specific kernels within a
/// single translation unit, selected at runtime by `__builtin_cpu_supports`.

#pragma once

#include <cstddef>
#include <new>

// ── Function target-attribute macros ─────────────────────────────────────────
//
// Apply these to function *definitions* (and matching declarations) whose body
// uses SIMD intrinsics.  The compiler generates ISA-specific code for that
// function only, without changing the global compilation target.
//
// Example:
//   G4OCCT_TARGET_AVX2
//   void MyFunc_avx2(...) { /* may use _mm256_* intrinsics */ }
//
// These macros are non-empty only on x86/x86-64 with GCC or Clang, where
// __attribute__((target(...))) is well-defined.  On all other architectures
// (AArch64, RISC-V, …) they expand to nothing so the code still compiles.

#if (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__))
/// Mark a function to be compiled for the AVX2 + FMA instruction set.
#  define G4OCCT_TARGET_AVX2    __attribute__((target("avx2,fma")))
/// Explicit default-target marker (no ISA extension required).
#  define G4OCCT_TARGET_DEFAULT __attribute__((target("default")))
#else
#  define G4OCCT_TARGET_AVX2
#  define G4OCCT_TARGET_DEFAULT
#endif

// ── Runtime CPU capability checks ────────────────────────────────────────────
//
// Use these in dispatch functions to select the widest supported ISA at
// runtime.  Both evaluate to `false` constants when `G4OCCT_USE_SIMD` is
// not defined (i.e. the library was built with `-DUSE_SIMD=OFF`), when the
// compiler does not provide `__builtin_cpu_supports`, or when building for a
// non-x86 architecture (where the builtin is not available or meaningful).

#if defined(G4OCCT_USE_SIMD) && (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__))
/// True at runtime when the executing CPU supports AVX2.
#  define G4OCCT_CPU_HAS_AVX2  (__builtin_cpu_supports("avx2"))
#else
#  define G4OCCT_CPU_HAS_AVX2  false
#endif

// ── Auto-vectorisation hint ───────────────────────────────────────────────────

/// Cross-compiler hint to suppress dependency analysis and enable
/// auto-vectorisation of the immediately following loop.
///
/// Usage:
/// ```cpp
/// G4OCCT_IVDEP
/// for (std::size_t i = 0; i < n; ++i) { ... }
/// ```
#if defined(__clang__)
#  define G4OCCT_IVDEP _Pragma("clang loop vectorize(enable)")
#elif defined(__GNUC__)
#  define G4OCCT_IVDEP _Pragma("GCC ivdep")
#else
#  define G4OCCT_IVDEP
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
