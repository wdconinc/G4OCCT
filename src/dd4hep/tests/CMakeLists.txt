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

# ── DD4hep standard elements file ────────────────────────────────────────────
#
# DD4hep 1.35 deliberately resets the ROOT TGeoElementTable to empty in
# DetectorImp's constructor, then fills it only from compact XML.
# TGeoMixture::AddElement(TGeoElement*, Double_t) internally calls
# AddElement(A, Z, w) which checks z > table->GetNelements()-1.  If the table
# contains only a handful of entries, any element with a high Z will trigger a
# ROOT Fatal.
#
# The fix is to include DD4hep's standard elements.xml (which defines all Z=1..
# Z=118 elements) before any material definitions.  After including it the
# table has ~119 entries, so AddElement never fires the z-range check.
#
# elements.xml is installed by DD4hep's DDDetectors package to:
#   ${DD4hep_ROOT}/DDDetectors/compact/elements.xml
# DD4hep_ROOT is exported by DD4hepConfig.cmake as the installation prefix.
find_file(
  G4OCCT_DD4HEP_ELEMENTS_XML
  NAMES elements.xml
  HINTS "${DD4hep_ROOT}/DDDetectors/compact"
        "${DD4hep_DIR}/../../../DDDetectors/compact"
  REQUIRED)
message(STATUS "G4OCCT DD4hep elements: ${G4OCCT_DD4HEP_ELEMENTS_XML}")

# Generate compact XML files with the absolute fixture paths substituted in.
configure_file(compact_step_solid.xml.in
               "${CMAKE_CURRENT_BINARY_DIR}/compact_step_solid.xml" @ONLY)
configure_file(compact_step_assembly.xml.in
               "${CMAKE_CURRENT_BINARY_DIR}/compact_step_assembly.xml" @ONLY)

# ── test_dd4hep_plugins ───────────────────────────────────────────────────────

add_executable(test_dd4hep_plugins test_dd4hep_plugins.cc)

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
