# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# ─── B4c example ─────────────────────────────────────────────────────────────
# G4OCCT version of the Geant4 basic/B4c example. The absorber and gap layer
# shapes are loaded from STEP files via G4OCCTSolid; all other files are copied
# verbatim from the upstream Geant4 source.

add_executable(
  exampleB4c
  exampleB4c.cc
  src/ActionInitialization.cc
  src/CalorHit.cc
  src/CalorimeterSD.cc
  src/DetectorConstruction.cc
  src/EventAction.cc
  src/PrimaryGeneratorAction.cc
  src/RunAction.cc)

target_include_directories(exampleB4c
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(exampleB4c PRIVATE G4OCCT)

# Provide the path to the STEP files at compile time so DetectorConstruction can
# locate them without relying on the working directory.
target_compile_definitions(
  exampleB4c PRIVATE G4OCCT_B4C_STEP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/step")

# ─── Geometry overlap checking ───────────────────────────────────────────────
# Overlap checking calls Inside() many times during construction, which inflates
# profiling counters for that method. Disable for callgrind profiling runs by
# passing -DB4C_CHECK_OVERLAPS=OFF to cmake.
option(B4C_CHECK_OVERLAPS "Enable B4c geometry overlap checking in placements" ON)
target_compile_definitions(
  exampleB4c PRIVATE B4C_CHECK_OVERLAPS=$<BOOL:${B4C_CHECK_OVERLAPS}>)

# ─── Valgrind/callgrind instrumentation ──────────────────────────────────────
# When VALGRIND_INCLUDE_DIR is set (e.g., -DVALGRIND_INCLUDE_DIR=/opt/local/include),
# expose CALLGRIND macros so RunAction can gate instrumentation around the run.
if(VALGRIND_INCLUDE_DIR)
  target_compile_definitions(exampleB4c PRIVATE HAVE_VALGRIND_CALLGRIND_H)
  target_include_directories(exampleB4c PRIVATE "${VALGRIND_INCLUDE_DIR}")
endif()

# ─── CTest integration ───────────────────────────────────────────────────────

add_test(NAME run_exampleB4c COMMAND exampleB4c -m
                                     ${CMAKE_CURRENT_SOURCE_DIR}/exampleB4.in)

