# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(
  G4OCCT
  VERSION 0.1.0
  DESCRIPTION
    "Geant4 interface to Open CASCADE Technology (OCCT) geometry definitions"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Compiler feature checks ──────────────────────────────────────────────────

include(CheckCXXCompilerFlag)

# ─── Warnings ─────────────────────────────────────────────────────────────────

option(ENABLE_WERROR "Treat compiler warnings as errors (-Werror)" OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall)
  if(ENABLE_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

# ─── External dependencies ────────────────────────────────────────────────────

find_package(Geant4 11.3 REQUIRED)
find_package(OpenCASCADE REQUIRED)

# ─── Library ─────────────────────────────────────────────────────────────────

add_library(G4OCCT src/G4OCCTSolid.cc src/G4OCCTLogicalVolume.cc
                   src/G4OCCTPlacement.cc)
add_library(G4OCCT::G4OCCT ALIAS G4OCCT)

target_include_directories(
  G4OCCT PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>)

target_include_directories(G4OCCT SYSTEM PUBLIC ${Geant4_INCLUDE_DIRS}
                                                ${OpenCASCADE_INCLUDE_DIR})

target_link_libraries(G4OCCT PUBLIC ${Geant4_LIBRARIES}
                                    ${OpenCASCADE_LIBRARIES})

# ─── Tests ────────────────────────────────────────────────────────────────────

include(CTest)

# ─── Benchmarks ───────────────────────────────────────────────────────────────

option(BUILD_BENCHMARKS "Build navigator benchmark programs" OFF)

# ─── Internal fixture comparison support ──────────────────────────────────────

if(BUILD_TESTING OR BUILD_BENCHMARKS)
  find_package(yaml-cpp 0.8 REQUIRED)
  add_library(
    G4OCCTFixtureSupport
    src/tests/geometry/fixture_manifest.cc
    src/tests/geometry/fixture_validation.cc
    src/tests/geometry/fixture_ray_compare.cc)
  target_include_directories(
    G4OCCTFixtureSupport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/tests ${Geant4_INCLUDE_DIRS}
           ${OpenCASCADE_INCLUDE_DIR})
  target_compile_definitions(
    G4OCCTFixtureSupport
    PUBLIC G4OCCT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/tests")
  target_link_libraries(
    G4OCCTFixtureSupport PUBLIC G4OCCT ${Geant4_LIBRARIES}
                                ${OpenCASCADE_LIBRARIES} yaml-cpp::yaml-cpp)
endif()

if(BUILD_TESTING)
  add_subdirectory(src/tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(src/benchmarks)
endif()

# ─── Sanitizers ───────────────────────────────────────────────────────────────

# Address sanitizer (also enables leak sanitizer via detect_leaks=1 at runtime)
option(USE_ASAN "Compile with address sanitizer" OFF)
if(USE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=address)
endif()

# Undefined behavior sanitizer
option(USE_UBSAN "Compile with undefined behavior sanitizer" OFF)
if(USE_UBSAN)
  foreach(sanitizer undefined float-divide-by-zero unsigned-integer-overflow
                    implicit-conversion local-bounds nullability)
    check_cxx_compiler_flag("-fsanitize=${sanitizer}"
                            CXX_COMPILER_HAS_sanitize_${sanitizer})
    if(CXX_COMPILER_HAS_sanitize_${sanitizer})
      add_compile_options(-fsanitize=${sanitizer})
      add_link_options(-fsanitize=${sanitizer})
    endif()
  endforeach()
  add_compile_options(-g -O1)
endif()

# Thread sanitizer (incompatible with address sanitizer and leak sanitizer)
option(USE_TSAN "Compile with thread sanitizer" OFF)
if(USE_TSAN)
  if(USE_ASAN)
    message(
      FATAL_ERROR
        "USE_TSAN is incompatible with USE_ASAN (and the leak sanitizer it implies). Enable only one sanitizer at a time."
    )
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=thread)
endif()

# ─── Examples ─────────────────────────────────────────────────────────────────

option(BUILD_EXAMPLES "Build G4OCCT usage examples" OFF)
if(BUILD_EXAMPLES)
  add_subdirectory(src/examples)
endif()

# ─── Installation ─────────────────────────────────────────────────────────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(
  TARGETS G4OCCT
  EXPORT G4OCCTTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  EXPORT G4OCCTTargets
  FILE G4OCCTTargets.cmake
  NAMESPACE G4OCCT::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/G4OCCT)

configure_package_config_file(
  cmake/G4OCCTConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/G4OCCT)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/G4OCCT)
