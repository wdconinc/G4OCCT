# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
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

# ─── CTest integration ───────────────────────────────────────────────────────

add_test(NAME run_exampleB4c COMMAND exampleB4c
                                     ${CMAKE_CURRENT_SOURCE_DIR}/exampleB4.in)
