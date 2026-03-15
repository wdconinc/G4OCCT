# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(G4OCCT
  VERSION 0.1.0
  DESCRIPTION "Geant4 interface to Open CASCADE Technology (OCCT) geometry definitions"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── External dependencies ────────────────────────────────────────────────────

find_package(Geant4 11.3 REQUIRED)
find_package(OpenCASCADE REQUIRED)

# ─── Library ─────────────────────────────────────────────────────────────────

add_library(G4OCCT
  src/G4OCCTSolid.cc
  src/G4OCCTLogicalVolume.cc
  src/G4OCCTPlacement.cc
)
add_library(G4OCCT::G4OCCT ALIAS G4OCCT)

target_include_directories(G4OCCT
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${Geant4_INCLUDE_DIRS}
    ${OpenCASCADE_INCLUDE_DIR}
)

target_link_libraries(G4OCCT
  PUBLIC
    ${Geant4_LIBRARIES}
    ${OpenCASCADE_LIBRARIES}
)

# ─── Tests ────────────────────────────────────────────────────────────────────

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(src/tests)
endif()

# ─── Benchmarks ───────────────────────────────────────────────────────────────

option(BUILD_BENCHMARKS "Build navigator benchmark programs" OFF)
if(BUILD_BENCHMARKS)
  add_subdirectory(src/benchmarks)
endif()

# ─── Examples ─────────────────────────────────────────────────────────────────

option(BUILD_EXAMPLES "Build G4OCCT usage examples" OFF)
if(BUILD_EXAMPLES)
  add_subdirectory(src/examples)
endif()

# ─── Installation ─────────────────────────────────────────────────────────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS G4OCCT
  EXPORT G4OCCTTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT G4OCCTTargets
  FILE G4OCCTTargets.cmake
  NAMESPACE G4OCCT::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/G4OCCT
)

configure_package_config_file(
  cmake/G4OCCTConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/G4OCCT
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/G4OCCT
)
