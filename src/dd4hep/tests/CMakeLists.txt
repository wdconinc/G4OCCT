# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# ── Paths to STEP fixtures reused from the core test suite ───────────────────
set(G4OCCT_STEP_SOLID_PATH
    "${CMAKE_SOURCE_DIR}/src/tests/fixtures/geometry/direct-primitives/G4Box/box-20x30x40-v1/shape.step"
)
set(G4OCCT_STEP_ASSEMBLY_PATH
    "${CMAKE_SOURCE_DIR}/src/tests/fixtures/assembly-comparison/triple-box-v1/shape.step"
)

# Generate compact XML files with the absolute fixture paths substituted in.
configure_file(compact_step_solid.xml.in
               "${CMAKE_CURRENT_BINARY_DIR}/compact_step_solid.xml" @ONLY)
configure_file(compact_step_assembly.xml.in
               "${CMAKE_CURRENT_BINARY_DIR}/compact_step_assembly.xml" @ONLY)

# ── test_dd4hep_plugins ───────────────────────────────────────────────────────

add_executable(test_dd4hep_plugins test_dd4hep_plugins.cc)

# DD4hep headers require C++17 (see parent CMakeLists.txt for rationale).
set_target_properties(test_dd4hep_plugins PROPERTIES CXX_STANDARD 17
                                                      CXX_STANDARD_REQUIRED OFF)

target_compile_definitions(
  test_dd4hep_plugins
  PRIVATE
    G4OCCT_COMPACT_STEP_SOLID="${CMAKE_CURRENT_BINARY_DIR}/compact_step_solid.xml"
    G4OCCT_COMPACT_STEP_ASSEMBLY="${CMAKE_CURRENT_BINARY_DIR}/compact_step_assembly.xml"
)

target_include_directories(test_dd4hep_plugins
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)

target_link_libraries(
  test_dd4hep_plugins
  PRIVATE G4OCCT::G4OCCT G4OCCTDD4hep DD4hep::DDCore GTest::gtest_main)

# ── CTest registration ────────────────────────────────────────────────────────

add_test(NAME dd4hep_step_solid
         COMMAND test_dd4hep_plugins --gtest_filter=STEPSolidTest.*)
add_test(NAME dd4hep_step_assembly
         COMMAND test_dd4hep_plugins --gtest_filter=STEPAssemblyTest.*)

set_tests_properties(dd4hep_step_solid dd4hep_step_assembly
                     PROPERTIES LABELS "dd4hep")
