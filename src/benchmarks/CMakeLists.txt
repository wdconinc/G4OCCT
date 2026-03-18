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
# the CALLGRIND_START/STOP_INSTRUMENTATION macros in the source.
include(CheckIncludeFileCXX)
check_include_file_cxx("valgrind/callgrind.h" HAVE_VALGRIND_CALLGRIND_H)
if(HAVE_VALGRIND_CALLGRIND_H)
  target_compile_definitions(bench_callgrind PRIVATE HAVE_VALGRIND_CALLGRIND_H)
endif()
