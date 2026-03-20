# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# ─── B1 example ──────────────────────────────────────────────────────────────
# G4OCCT version of the Geant4 basic/B1 example. Two shapes are loaded from STEP
# files via G4OCCTSolid; materials are specified programmatically with
# G4NistManager names.

add_executable(
  exampleB1
  exampleB1.cc
  src/DetectorConstruction.cc
  src/ActionInitialization.cc
  src/PrimaryGeneratorAction.cc
  src/EventAction.cc
  src/RunAction.cc
  src/SteppingAction.cc)

target_include_directories(exampleB1
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(exampleB1 PRIVATE G4OCCT)

# Provide the path to the STEP files at compile time so DetectorConstruction can
# locate them without relying on the working directory.
target_compile_definitions(
  exampleB1 PRIVATE G4OCCT_B1_STEP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/step")

# ─── CTest integration ───────────────────────────────────────────────────────

add_test(NAME run_exampleB1 COMMAND exampleB1
                                    ${CMAKE_CURRENT_SOURCE_DIR}/run.mac)
