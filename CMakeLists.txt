# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(
  G4OCCT
  VERSION 0.1.0
  DESCRIPTION
    "Geant4 interface to Open CASCADE Technology (OCCT) geometry definitions"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── CMake policies ───────────────────────────────────────────────────────────

# CMP0135: ExternalProject/FetchContent timestamp policy; set before
# FetchContent use.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# CMP0144: find_package uses upper-case <PACKAGENAME>_ROOT variables.
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

# CMP0167: The FindBoost module is removed; use upstream BoostConfig.cmake.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

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

# ─── Sanitizers ───────────────────────────────────────────────────────────────
# These must be configured before any target definitions so that
# add_compile_options / add_link_options apply to every target in this
# directory (G4OCCT library, tests, benchmarks, apps).

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

# ─── Coverage ─────────────────────────────────────────────────────────────────

# Code coverage (gcov/llvm-cov; incompatible with optimised builds)
option(USE_COVERAGE
       "Compile with code coverage instrumentation (gcov/llvm-cov)" OFF)
if(USE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "USE_COVERAGE requires a GCC or Clang compiler")
  endif()
  include(cmake/Coverage.cmake)
endif()

# ─── External dependencies ────────────────────────────────────────────────────

find_package(Geant4 11.3 REQUIRED)
find_package(OpenCASCADE REQUIRED)

# ─── RPATH ────────────────────────────────────────────────────────────────────

# Keep RPATH in the build tree (stated explicitly for clarity).
set(CMAKE_SKIP_BUILD_RPATH FALSE)
# Use build-tree paths during the build, not the install RPATH.
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
# Embed the link-time library directories of all linked targets (Geant4,
# OpenCASCADE, …) into the installed binary/library RPATH automatically.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# ─── Library ─────────────────────────────────────────────────────────────────

add_library(
  G4OCCT
  src/G4OCCTSolid.cc src/G4OCCTLogicalVolume.cc src/G4OCCTPlacement.cc
  src/G4OCCTMaterialMap.cc src/G4OCCTMaterialMapReader.cc
  src/G4OCCTAssemblyVolume.cc)
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

# ─── GoogleTest ───────────────────────────────────────────────────────────────

if(BUILD_TESTING)
  include(GoogleTest)
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
      URL_HASH
        SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7)
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
endif()

# ─── Benchmarks ───────────────────────────────────────────────────────────────

option(BUILD_BENCHMARKS "Build navigator benchmark programs" OFF)

# ─── GoogleBenchmark ──────────────────────────────────────────────────────────

if(BUILD_BENCHMARKS)
  find_package(benchmark QUIET)
  if(NOT benchmark_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googlebenchmark
      URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
      URL_HASH
        SHA256=32131c08ee31eeff2c8968d7e874f3cb648034377dfc32a4c377fa8796d84981)
    set(BENCHMARK_ENABLE_TESTING
        OFF
        CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS
        OFF
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
  endif()
endif()

# ─── Internal fixture comparison support ──────────────────────────────────────

if(BUILD_TESTING OR BUILD_BENCHMARKS)
  find_package(yaml-cpp 0.8 REQUIRED)
  find_package(ZLIB REQUIRED)
  add_library(
    G4OCCTFixtureSupport
    src/tests/geometry/fixture_manifest.cc
    src/tests/geometry/fixture_solid_builder.cc
    src/tests/geometry/fixture_validation.cc
    src/tests/geometry/fixture_solid_builder.cc
    src/tests/geometry/fixture_ray_compare.cc
    src/tests/geometry/fixture_inside_compare.cc
    src/tests/geometry/fixture_safety_compare.cc
    src/tests/geometry/fixture_polyhedron_compare.cc)
  target_include_directories(
    G4OCCTFixtureSupport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/tests ${Geant4_INCLUDE_DIRS}
           ${OpenCASCADE_INCLUDE_DIR})
  target_compile_definitions(
    G4OCCTFixtureSupport
    PUBLIC G4OCCT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/tests")
  target_link_libraries(
    G4OCCTFixtureSupport
    PUBLIC G4OCCT ${Geant4_LIBRARIES} ${OpenCASCADE_LIBRARIES}
           yaml-cpp::yaml-cpp ZLIB::ZLIB)
endif()

if(BUILD_TESTING)
  add_subdirectory(src/tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(src/benchmarks)
endif()

# ─── Examples ─────────────────────────────────────────────────────────────────

option(BUILD_EXAMPLES "Build G4OCCT usage examples" OFF)
if(BUILD_EXAMPLES)
  add_subdirectory(src/examples)
endif()

# ─── DD4hep plugin ────────────────────────────────────────────────────────────

option(BUILD_DD4HEP_PLUGIN
       "Build DD4hep detector element plugins (requires DD4hep)" OFF)

if(BUILD_DD4HEP_PLUGIN)
  find_package(DD4hep QUIET)
  if(NOT DD4hep_FOUND)
    message(FATAL_ERROR "BUILD_DD4HEP_PLUGIN=ON but DD4hep was not found. "
                        "The DD4hep plugin will not be built.")
  else()
    add_subdirectory(src/dd4hep)
  endif()
endif()

# ─── App ──────────────────────────────────────────────────────────────────────

include(GNUInstallDirs)

add_subdirectory(src/app/g4occt)

# ─── Installation ─────────────────────────────────────────────────────────────

include(CMakePackageConfigHelpers)

install(
  TARGETS G4OCCT
  EXPORT G4OCCTTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_DD4HEP_PLUGIN AND DD4hep_FOUND)
  install(TARGETS G4OCCTDD4hep LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

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
