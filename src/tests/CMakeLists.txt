# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

cmake_minimum_required(VERSION 3.16)

# ─── test_solid ───────────────────────────────────────────────────────────────
add_executable(test_solid test_solid.cc)
target_link_libraries(test_solid PRIVATE G4OCCT)
add_test(NAME test_solid COMMAND test_solid)

# ─── test_logical_volume ──────────────────────────────────────────────────────
add_executable(test_logical_volume test_logical_volume.cc)
target_link_libraries(test_logical_volume PRIVATE G4OCCT)
add_test(NAME test_logical_volume COMMAND test_logical_volume)

# ─── test_placement ───────────────────────────────────────────────────────────
add_executable(test_placement test_placement.cc)
target_link_libraries(test_placement PRIVATE G4OCCT)
add_test(NAME test_placement COMMAND test_placement)

# ─── test_navigation_harness ──────────────────────────────────────────────────
add_executable(test_navigation_harness test_navigation_harness.cc)
target_link_libraries(test_navigation_harness PRIVATE G4OCCT)
add_test(NAME test_navigation_harness COMMAND test_navigation_harness)

# ─── test_navigation_inside ───────────────────────────────────────────────────
add_executable(test_navigation_inside test_navigation_inside.cc)
target_link_libraries(test_navigation_inside PRIVATE G4OCCT)
add_test(NAME test_navigation_inside COMMAND test_navigation_inside)

# ─── test_surface_normal ──────────────────────────────────────────────────────
add_executable(test_surface_normal test_surface_normal.cc)
target_link_libraries(test_surface_normal PRIVATE G4OCCT)
add_test(NAME test_surface_normal COMMAND test_surface_normal)

# ─── test_distance_to_in_ray ──────────────────────────────────────────────────
add_executable(test_distance_to_in_ray test_distance_to_in_ray.cc)
target_link_libraries(test_distance_to_in_ray PRIVATE G4OCCT)
add_test(NAME test_distance_to_in_ray COMMAND test_distance_to_in_ray)

# ─── test_distance_to_in_safety ───────────────────────────────────────────────
add_executable(test_distance_to_in_safety test_distance_to_in_safety.cc)
target_link_libraries(test_distance_to_in_safety PRIVATE G4OCCT)
add_test(NAME test_distance_to_in_safety COMMAND test_distance_to_in_safety)

# ─── test_distance_to_out_ray ─────────────────────────────────────────────────
add_executable(test_distance_to_out_ray test_distance_to_out_ray.cc)
target_link_libraries(test_distance_to_out_ray PRIVATE G4OCCT)
add_test(NAME test_distance_to_out_ray COMMAND test_distance_to_out_ray)

# ─── test_distance_to_out_safety ──────────────────────────────────────────────
add_executable(test_distance_to_out_safety test_distance_to_out_safety.cc)
target_link_libraries(test_distance_to_out_safety PRIVATE G4OCCT)
add_test(NAME test_distance_to_out_safety COMMAND test_distance_to_out_safety)

# ─── test_get_extent ──────────────────────────────────────────────────────────
add_executable(test_get_extent test_get_extent.cc)
target_link_libraries(test_get_extent PRIVATE G4OCCT)
add_test(NAME test_get_extent COMMAND test_get_extent)

# ─── test_bounding_limits ─────────────────────────────────────────────────────
add_executable(test_bounding_limits test_bounding_limits.cc)
target_link_libraries(test_bounding_limits PRIVATE G4OCCT)
add_test(NAME test_bounding_limits COMMAND test_bounding_limits)

# ─── test_calculate_extent ────────────────────────────────────────────────────
add_executable(test_calculate_extent test_calculate_extent.cc)
target_link_libraries(test_calculate_extent PRIVATE G4OCCT)
add_test(NAME test_calculate_extent COMMAND test_calculate_extent)

# ─── test_create_polyhedron ───────────────────────────────────────────────────
add_executable(test_create_polyhedron test_create_polyhedron.cc)
target_link_libraries(test_create_polyhedron PRIVATE G4OCCT)
add_test(NAME test_create_polyhedron COMMAND test_create_polyhedron)

# ─── test_geometry_validation ────────────────────────────────────────────────
add_executable(test_geometry_validation
  geometry/test_geometry_validation.cc
  geometry/fixture_manifest.cc
  geometry/fixture_validation.cc
)
target_include_directories(test_geometry_validation
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCASCADE_INCLUDE_DIR}
)
target_compile_definitions(test_geometry_validation
  PRIVATE
    G4OCCT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_geometry_validation
  PRIVATE
    ${OpenCASCADE_LIBRARIES}
)
add_test(NAME test_geometry_validation COMMAND test_geometry_validation)

# ─── Integration test: downstream find_package(G4OCCT) ───────────────────────
# This test group installs G4OCCT to a temporary prefix and then configures,
# builds, and runs a minimal downstream CMake project that consumes the library
# via find_package(G4OCCT).  CTest fixtures are used to ensure the steps run
# in the correct order.

# Step 1: install the library to a temporary prefix
add_test(NAME install_g4occt
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
          --prefix ${CMAKE_BINARY_DIR}/test_install
)
set_tests_properties(install_g4occt PROPERTIES
  FIXTURES_SETUP G4OCCTInstall
)

# Step 2: configure the downstream project
add_test(NAME downstream_configure
  COMMAND ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_SOURCE_DIR}/downstream_test
    -B ${CMAKE_BINARY_DIR}/downstream_build
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/test_install
    -DCMAKE_BUILD_TYPE=Release
)
set_tests_properties(downstream_configure PROPERTIES
  FIXTURES_REQUIRED G4OCCTInstall
  FIXTURES_SETUP    DownstreamConfig
)

# Step 3: build the downstream project
add_test(NAME downstream_build
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/downstream_build
)
set_tests_properties(downstream_build PROPERTIES
  FIXTURES_REQUIRED DownstreamConfig
  FIXTURES_SETUP    DownstreamBuild
)

# Step 4: run the downstream application
add_test(NAME test_downstream_findpackage
  COMMAND ${CMAKE_BINARY_DIR}/downstream_build/downstream_app
)
set_tests_properties(test_downstream_findpackage PROPERTIES
  FIXTURES_REQUIRED DownstreamBuild
)
