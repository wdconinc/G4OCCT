# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# Helper macro: build a Google Benchmark executable. Usage:
# g4occt_benchmark(target sources... LINK_LIBRARIES libs...)
macro(g4occt_benchmark _target)
  cmake_parse_arguments(_arg "" "" "LINK_LIBRARIES" ${ARGN})
  add_executable(${_target} ${_arg_UNPARSED_ARGUMENTS})
  target_link_libraries(${_target} PRIVATE ${_arg_LINK_LIBRARIES}
                                           benchmark::benchmark)
  # Benchmarks are NOT added to CTest; run manually.
endmacro()

# ─── bench_navigator ─────────────────────────────────────────────────────────
g4occt_benchmark(bench_navigator bench_navigator.cc LINK_LIBRARIES
                 G4OCCTFixtureSupport)
# Run with: ./bench_navigator [N_rays] [manifest_path] [point_cloud_dir] or, to
# produce Google Benchmark JSON piped through the report script:
# ./bench_navigator --benchmark_format=json [args] | python3
# scripts/bench_report.py

# ─── bench_assembly_navigator ─────────────────────────────────────────────────
# Compares GDML-reference vs STEP-imported assembly ray casting. Requires
# G4GDMLParser (Geant4 built with GDML support) and zlib. Run with:
# ./bench_assembly_navigator [N_rays] [fixture_root] [point_cloud_dir]
#
# Before running the benchmark, generate the STEP fixture: bash
# src/tests/fixtures/assembly-comparison/regenerate.sh
#
# G4GDMLParser lives in Geant4's persistency module.  Use find_file (rather than
# check_include_file_cxx) so that the detection is not blocked by missing
# XercesC headers in CMAKE_REQUIRED_INCLUDES.  In environments such as
# eic_xl:nightly, Geant4 is built with GDML but XercesC lives at a non-standard
# Spack path that is not automatically appended to the compiler search path.
# check_include_file_cxx tries to compile the header, fails because
# G4GDMLParser.hh transitively includes <xercesc/...>, and therefore incorrectly
# reports GDML as absent.  find_file only checks whether the file exists on
# disk.
find_file(
  G4GDMLPARSER_HH
  NAMES "G4GDMLParser.hh"
  PATHS ${Geant4_INCLUDE_DIRS}
  NO_DEFAULT_PATH
  DOC "Path to G4GDMLParser.hh from a Geant4 GDML-enabled installation")

if(G4GDMLPARSER_HH)
  # G4GDMLParser.hh transitively includes <xercesc/...> headers, so XercesC must
  # be on the include path at compile time.  Any Geant4 installation with GDML
  # enabled implies XercesC is present; QUIET suppresses the redundant "found"
  # message (the package was already located via Geant4Config.cmake). Guard the
  # entire bench_assembly_navigator build on the XercesC::XercesC target being
  # available so the outcome is always deterministic: if XercesC cannot be found
  # the benchmark is skipped with an explicit STATUS message rather than created
  # but left unable to compile.
  find_package(XercesC QUIET)
  if(TARGET XercesC::XercesC)
    g4occt_benchmark(bench_assembly_navigator bench_assembly_navigator.cc
                     LINK_LIBRARIES G4OCCT ZLIB::ZLIB XercesC::XercesC)
    # Pass the source-tree fixture directory so the benchmark can locate
    # geometry.gdml files even when run from a build directory.
    target_compile_definitions(
      bench_assembly_navigator
      PRIVATE "G4OCCT_TEST_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../tests\"")
    message(
      STATUS "Found G4GDMLParser.hh; bench_assembly_navigator will be built")
  else()
    message(
      STATUS "XercesC not found; bench_assembly_navigator will not be built")
  endif()
else()
  message(
    STATUS
      "G4GDMLParser.hh not found; bench_assembly_navigator will not be built")
endif()

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
  if(TARGET bench_assembly_navigator)
    target_compile_definitions(bench_assembly_navigator
                               PRIVATE HAVE_VALGRIND_CALLGRIND_H)
    target_include_directories(bench_assembly_navigator
                               PRIVATE "${VALGRIND_INCLUDE_DIR}")
  endif()
else()
  message(
    STATUS "valgrind/callgrind.h not found; CALLGRIND_* macros will be no-ops")
endif()
