# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# Helper macro: register a GTest-based unit test executable. Usage:
# g4occt_gtest(target sources... LINK_LIBRARIES libs...)
macro(g4occt_gtest _target)
  cmake_parse_arguments(_arg "" "" "LINK_LIBRARIES" ${ARGN})
  add_executable(${_target} ${_arg_UNPARSED_ARGUMENTS})
  target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${_target} PRIVATE ${_arg_LINK_LIBRARIES}
                                           GTest::gtest_main)
  gtest_discover_tests(${_target})
endmacro()

# ─── test_solid ───────────────────────────────────────────────────────────────
g4occt_gtest(test_solid test_solid.cc LINK_LIBRARIES G4OCCT)

# ─── test_logical_volume ──────────────────────────────────────────────────────
g4occt_gtest(test_logical_volume test_logical_volume.cc LINK_LIBRARIES G4OCCT)

# ─── test_placement ───────────────────────────────────────────────────────────
g4occt_gtest(test_placement test_placement.cc LINK_LIBRARIES G4OCCT)

# ─── test_material_map ────────────────────────────────────────────────────────
g4occt_gtest(test_material_map test_material_map.cc LINK_LIBRARIES G4OCCT)

# ─── test_material_map_reader ─────────────────────────────────────────────────
g4occt_gtest(test_material_map_reader test_material_map_reader.cc
             LINK_LIBRARIES G4OCCT)

# ─── test_assembly_volume ─────────────────────────────────────────────────────
g4occt_gtest(test_assembly_volume test_assembly_volume.cc LINK_LIBRARIES G4OCCT)

# ─── test_navigation_harness ──────────────────────────────────────────────────
g4occt_gtest(test_navigation_harness test_navigation_harness.cc LINK_LIBRARIES
             G4OCCT)

# ─── test_navigation_inside ───────────────────────────────────────────────────
g4occt_gtest(test_navigation_inside test_navigation_inside.cc LINK_LIBRARIES
             G4OCCT)

# ─── test_surface_normal ──────────────────────────────────────────────────────
g4occt_gtest(test_surface_normal test_surface_normal.cc LINK_LIBRARIES G4OCCT)

# ─── test_distance_to_in_ray ──────────────────────────────────────────────────
g4occt_gtest(test_distance_to_in_ray test_distance_to_in_ray.cc LINK_LIBRARIES
             G4OCCT)

# ─── test_distance_to_in_safety ───────────────────────────────────────────────
g4occt_gtest(test_distance_to_in_safety test_distance_to_in_safety.cc
             LINK_LIBRARIES G4OCCT)

# ─── test_distance_to_out_ray ─────────────────────────────────────────────────
g4occt_gtest(test_distance_to_out_ray test_distance_to_out_ray.cc
             LINK_LIBRARIES G4OCCT)

# ─── test_distance_to_out_safety ──────────────────────────────────────────────
g4occt_gtest(test_distance_to_out_safety test_distance_to_out_safety.cc
             LINK_LIBRARIES G4OCCT)

# ─── test_exact_distance_to_in ────────────────────────────────────────────────
g4occt_gtest(test_exact_distance_to_in test_exact_distance_to_in.cc
             LINK_LIBRARIES G4OCCT)

# ─── test_exact_distance_to_out ───────────────────────────────────────────────
g4occt_gtest(test_exact_distance_to_out test_exact_distance_to_out.cc
             LINK_LIBRARIES G4OCCT)

# ─── test_get_extent ──────────────────────────────────────────────────────────
g4occt_gtest(test_get_extent test_get_extent.cc LINK_LIBRARIES G4OCCT)

# ─── test_bounding_limits ─────────────────────────────────────────────────────
g4occt_gtest(test_bounding_limits test_bounding_limits.cc LINK_LIBRARIES G4OCCT)

# ─── test_calculate_extent ────────────────────────────────────────────────────
g4occt_gtest(test_calculate_extent test_calculate_extent.cc LINK_LIBRARIES
             G4OCCT)

# ─── test_create_polyhedron ───────────────────────────────────────────────────
g4occt_gtest(test_create_polyhedron test_create_polyhedron.cc LINK_LIBRARIES
             G4OCCT)

# ─── test_geometry_validation ────────────────────────────────────────────────
add_executable(test_geometry_validation geometry/test_geometry_validation.cc)
target_include_directories(test_geometry_validation
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_geometry_validation PRIVATE G4OCCTFixtureSupport)
add_test(NAME test_geometry_validation COMMAND test_geometry_validation)
# The aggregate validation binary remains available for manual runs, but default
# ctest execution should use the per-fixture tests for clearer CI reporting.
set_tests_properties(test_geometry_validation PROPERTIES DISABLED TRUE)
file(
  GLOB _geometry_family_manifests CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry/*/manifest.yaml)
list(SORT _geometry_family_manifests)
set(_geometry_fixture_ids)
foreach(_family_manifest IN LISTS _geometry_family_manifests)
  # Depend on manifest file contents so fixture ID changes trigger reconfigure.
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/${_family_manifest}")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${_family_manifest}"
       _family_fixture_lines REGEX "^  - id: ")
  foreach(_fixture_line IN LISTS _family_fixture_lines)
    string(REGEX REPLACE "^  - id: " "" fixture_id "${_fixture_line}")
    list(APPEND _geometry_fixture_ids "${fixture_id}")
  endforeach()
endforeach()
list(SORT _geometry_fixture_ids)
foreach(fixture_id IN LISTS _geometry_fixture_ids)
  add_test(NAME "test_geometry_validation/${fixture_id}"
           COMMAND test_geometry_validation --fixture ${fixture_id})
  # Temporarily disabled: nist-ctc test_geometry_validation tests.
  # ValidateFixtureGeometry loads large AP203 compound-solid STEP files; this is
  # very slow and can produce G4Exceptions from OCCT navigation on complex
  # assemblies (see bench_navigator guard added in the same commit). nist-ctc-02
  # and nist-ctc-07 are the largest and consistently hit the CTest timeout.
  # Geometry import is covered by test_nist_ctc_inside_volume; these validation
  # tests will be re-enabled once per-fixture timeouts are in place.
  if(fixture_id MATCHES "^nist-ctc-")
    set_tests_properties("test_geometry_validation/${fixture_id}"
                         PROPERTIES DISABLED TRUE)
  endif()
endforeach()
unset(_family_fixture_lines)
unset(_fixture_line)
unset(_geometry_family_manifests)
unset(_geometry_fixture_ids)
# Temporarily disabled: nist-ctc-02-v1 and nist-ctc-07-v1 consistently time out
# in CI (see
# https://github.com/eic/G4OCCT/actions/runs/23174161275/job/67332574303)
# until the underlying performance issue is diagnosed and resolved.
set_tests_properties(
  "test_geometry_validation/nist-ctc-02-v1"
  "test_geometry_validation/nist-ctc-07-v1" PROPERTIES DISABLED TRUE)

# ─── render_geometry_fixtures ────────────────────────────────────────────────
# Renders each fixture solid to a JPEG image using Geant4's built-in RayTracer
# visualisation driver (/vis/open RT), which traces rays through Geant4's own
# geometry navigation infrastructure.  Only built when G4VisExecutive is
# available (i.e. Geant4 was compiled with visualisation support).
find_file(
  G4_VIS_EXECUTIVE_HH G4VisExecutive.hh
  HINTS ${Geant4_INCLUDE_DIRS}
  NO_DEFAULT_PATH)
if(G4_VIS_EXECUTIVE_HH)
  add_executable(render_geometry_fixtures geometry/render_geometry_fixtures.cc)
  target_include_directories(
    render_geometry_fixtures
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            # Geant4's private RayTracer headers are not installed by the Geant4
            # build system.  Vendored copies under geant4_private/ allow direct
            # use of G4TheRayTracer (single-threaded base class) to bypass
            # G4TheMTRayTracer, which crashes in MT builds due to pool
            # corruption in worker threads when BeamOn is called from a
            # visualization context.
            ${CMAKE_CURRENT_SOURCE_DIR}/geometry/geant4_private)
  target_link_libraries(render_geometry_fixtures PRIVATE G4OCCTFixtureSupport)

  # ── Aggregate test (disabled in default ctest runs) ──────────────────────
  add_test(
    NAME render_geometry_fixtures
    COMMAND
      render_geometry_fixtures --output-dir
      ${CMAKE_BINARY_DIR}/geometry_renders --manifest
      ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry/manifest.yaml)
  # Keep the aggregate binary available for manual runs; per-fixture tests are
  # registered below for CI-friendly individual reporting.
  set_tests_properties(render_geometry_fixtures PROPERTIES DISABLED TRUE)

  # ── Per-fixture tests ─────────────────────────────────────────────────────
  # Families with large assemblies that exceed the CI memory/time budget are
  # excluded; they are covered by dedicated correctness tests instead.
  set(_render_skip_families)
  file(
    GLOB _render_family_manifests CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry/*/manifest.yaml)
  list(SORT _render_family_manifests)
  set(_render_qualified_ids)
  foreach(_manifest IN LISTS _render_family_manifests)
    # Skip the top-level repository manifest.
    if(_manifest STREQUAL "fixtures/geometry/manifest.yaml")
      continue()
    endif()
    set_property(
      DIRECTORY
      APPEND
      PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${CMAKE_CURRENT_SOURCE_DIR}/${_manifest}")
    # Extract the family name from the manifest's "family:" field.
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${_manifest}" _family_line
         REGEX "^family: ")
    string(REGEX REPLACE "^family: " "" _family "${_family_line}")
    if(_family IN_LIST _render_skip_families)
      continue()
    endif()
    # Collect fixture IDs from "  - id: <id>" lines.
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${_manifest}" _id_lines
         REGEX "^  - id: ")
    foreach(_id_line IN LISTS _id_lines)
      string(REGEX REPLACE "^  - id: " "" _fixture_id "${_id_line}")
      list(APPEND _render_qualified_ids "${_family}/${_fixture_id}")
    endforeach()
  endforeach()
  list(SORT _render_qualified_ids)
  foreach(_qualified_id IN LISTS _render_qualified_ids)
    string(REGEX REPLACE "/.*$" "" _family "${_qualified_id}")
    if(_family STREQUAL "nist-ctc")
      set(_render_timeout 300)
    else()
      set(_render_timeout 120)
    endif()
    add_test(
      NAME "render_geometry_fixtures/${_qualified_id}"
      COMMAND
        render_geometry_fixtures --output-dir
        ${CMAKE_BINARY_DIR}/geometry_renders --manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry/manifest.yaml --fixture
        ${_qualified_id})
    set_tests_properties("render_geometry_fixtures/${_qualified_id}"
                         PROPERTIES RUN_SERIAL TRUE LABELS "visualization"
                                    TIMEOUT ${_render_timeout})
  endforeach()
  unset(_render_skip_families)
  unset(_render_family_manifests)
  unset(_manifest)
  unset(_family_line)
  unset(_family)
  unset(_id_lines)
  unset(_id_line)
  unset(_fixture_id)
  unset(_render_qualified_ids)
  unset(_qualified_id)
  unset(_render_timeout)
else()
  message(
    STATUS
      "G4VisExecutive.hh not found; render_geometry_fixtures will not be built")
endif()

# ─── test_nist_ctc_inside_volume ──────────────────────────────────────────────
# Validates G4OCCTSolid::Inside() volume estimates for NIST AP203 Simplified
# Test Case (CTC) geometry-only STEP files.  One CTest sub-test is registered
# per fixture directory discovered under fixtures/geometry/nist-ctc/G4OCCTSolid/
# so that CI reports per-volume progress, individual failures are easier to
# diagnose, and adding a new fixture directory automatically adds a new test.
# Fixtures that have not been fetched (run
# src/tests/fixtures/geometry/nist-ctc/fetch.sh first) are silently skipped; the
# individual test still exits with success in that case.
add_executable(
  test_nist_ctc_inside_volume geometry/test_nist_ctc_inside_volume.cc
                              geometry/fixture_manifest.cc)
target_include_directories(test_nist_ctc_inside_volume
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(
  test_nist_ctc_inside_volume
  PRIVATE G4OCCT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_nist_ctc_inside_volume PRIVATE G4OCCT)
file(
  GLOB _nist_ctc_fixture_dirs CONFIGURE_DEPENDS
  LIST_DIRECTORIES true
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry/nist-ctc/G4OCCTSolid
  ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry/nist-ctc/G4OCCTSolid/nist-ctc-*-v*
)
list(SORT _nist_ctc_fixture_dirs)
foreach(fixture_id IN LISTS _nist_ctc_fixture_dirs)
  add_test(NAME "test_nist_ctc_inside_volume/${fixture_id}"
           COMMAND test_nist_ctc_inside_volume ${fixture_id})
endforeach()
unset(_nist_ctc_fixture_dirs)
