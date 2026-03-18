# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# ─── bench_navigator ─────────────────────────────────────────────────────────
add_executable(bench_navigator bench_navigator.cc)
target_link_libraries(bench_navigator PRIVATE G4OCCTFixtureSupport)

# Benchmarks are not added to CTest by default; run manually with:
# ./bench_navigator [N_rays] [manifest_path]

# ─── bench_callgrind ─────────────────────────────────────────────────────────
# Single-fixture callgrind-instrumented navigator benchmark.  Instrumentation
# starts only when the navigation hot loop begins (after warm-up), so the
# callgrind profile captures steady-state costs without setup or STEP loading.
#
# Run under callgrind with: valgrind --tool=callgrind --instr-atstart=no
# --collect-atstart=no \ ./bench_callgrind [fixture_id] [manifest_path]
# [ray_count] callgrind_annotate --auto=yes callgrind.out.<PID> >
# callgrind_annotate.txt
add_executable(bench_callgrind bench_callgrind.cc)
target_link_libraries(bench_callgrind PRIVATE G4OCCTFixtureSupport)

# Detect whether the valgrind/callgrind.h header is available and, if so, enable
# the CALLGRIND_START/STOP_INSTRUMENTATION and CALLGRIND_TOGGLE_COLLECT macros
# in the source.  Use find_path so the include directory is recorded and can be
# added to the target; check_include_file_cxx only checks compilation but does
# not expose the path needed for target_include_directories.
#
# When the headers are in a non-standard location (e.g. /opt/local/include
# inside a container), pass -DVALGRIND_INCLUDE_DIR=<dir> to cmake.
find_path(
  VALGRIND_INCLUDE_DIR
  NAMES "valgrind/callgrind.h"
  DOC "Directory containing valgrind/callgrind.h")
if(VALGRIND_INCLUDE_DIR)
  message(STATUS "Found valgrind/callgrind.h in: ${VALGRIND_INCLUDE_DIR}")
  target_compile_definitions(bench_callgrind PRIVATE HAVE_VALGRIND_CALLGRIND_H)
  target_include_directories(bench_callgrind PRIVATE "${VALGRIND_INCLUDE_DIR}")
else()
  message(
    STATUS "valgrind/callgrind.h not found; CALLGRIND_* macros will be no-ops")
endif()
