# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# ─── DD4hep detector-element plugins ─────────────────────────────────────────
#
# dd4hep_add_plugin() is provided by DD4hepConfig.cmake.  It produces a shared
# library (libG4OCCTDD4hep.so) and registers it with DD4hep's factory system.

dd4hep_add_plugin(
  G4OCCTDD4hep
  SOURCES G4OCCT_STEPSolid.cc G4OCCT_STEPAssembly.cc)

# DD4hep headers use C++17 patterns (implicit const Handle conversions) that
# are not compatible with C++20's stricter const rules.  Compile the plugin
# with C++17 to match the C++ standard DD4hep was built with in the container.
set_target_properties(G4OCCTDD4hep PROPERTIES CXX_STANDARD 17
                                               CXX_STANDARD_REQUIRED OFF)

target_link_libraries(G4OCCTDD4hep PRIVATE G4OCCT::G4OCCT DD4hep::DDCore)

# ─── Tests ────────────────────────────────────────────────────────────────────

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
