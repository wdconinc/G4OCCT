// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/**
 * @file callgrind_macros.hh
 * @brief Callgrind instrumentation macros for benchmarks.
 *
 * When the binary runs outside valgrind, or when the valgrind headers are
 * absent at build time, all CALLGRIND_* macros expand to no-ops so that
 * benchmark binaries remain usable without valgrind installed.
 *
 * Include this header in any benchmark that gates instrumentation via
 * CALLGRIND_START_INSTRUMENTATION / CALLGRIND_TOGGLE_COLLECT /
 * CALLGRIND_STOP_INSTRUMENTATION.
 */

#pragma once

#ifdef HAVE_VALGRIND_CALLGRIND_H
#include <valgrind/callgrind.h>
#else
// clang-format off
#define CALLGRIND_START_INSTRUMENTATION do { } while (0)
#define CALLGRIND_STOP_INSTRUMENTATION  do { } while (0)
#define CALLGRIND_TOGGLE_COLLECT        do { } while (0)
// clang-format on
#endif
