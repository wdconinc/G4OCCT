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
  SOURCES G4OCCT_STEPSolid.cc   G4OCCT_STEPSolid_impl.cc
          G4OCCT_STEPAssembly.cc G4OCCT_STEPAssembly_impl.cc)

# DD4hep headers use C++17 patterns (implicit const Handle<T> conversions) that
# are rejected under C++20's stricter const rules.  Compile the plugin with
# C++17 to match the C++ standard DD4hep was built with in the container.
# CXX_STANDARD_REQUIRED OFF allows a higher standard if the toolchain requires.
set_target_properties(G4OCCTDD4hep PROPERTIES
  CXX_STANDARD          17
  CXX_STANDARD_REQUIRED OFF)

target_link_libraries(G4OCCTDD4hep PRIVATE G4OCCT::G4OCCT DD4hep::DDCore)

# ─── Rootmap (components file) ────────────────────────────────────────────────
#
# dd4hep_add_plugin() automatically creates a Components_G4OCCTDD4hep target
# that runs DD4hep's listcomponents binary to generate libG4OCCTDD4hep.components.
# listcomponents is a pre-built binary in the container; it is NOT instrumented
# with address or thread sanitizers.  When USE_ASAN or USE_TSAN is ON our plugin
# is ASAN/TSAN-instrumented, so dlopen inside listcomponents fails with:
#   undefined symbol: __asan_option_detect_stack_use_after_return
#
# The .components file is only needed by the standalone dd4hep-plugin CI job.
# Sanitizer jobs exclude DD4hep tests (-LE dd4hep) and do not need this file.
# Exclude the Components target from the default build when sanitizers are ON
# rather than adopting ownership of DD4hep's dd4hep_generate_rootmap function.
if(USE_ASAN OR USE_TSAN)
  set_target_properties(Components_G4OCCTDD4hep PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
