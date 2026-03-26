# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

add_executable(
  g4occt
  main.cc
  G4OCCTDetectorConstruction.cc
  G4OCCTActionInitialization.cc)

target_include_directories(g4occt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(g4occt PRIVATE G4OCCT::G4OCCT ${Geant4_LIBRARIES})

# ── Macro data directory (FHS: share/g4occt/macros) ──────────────────────────

# Per the Linux FHS, architecture-independent application data lives under
# ${CMAKE_INSTALL_DATADIR}/<package>.  The build-tree copy goes into
# ${CMAKE_CURRENT_BINARY_DIR}/macros/ so the layout mirrors the install tree.

set(G4OCCT_MACRO_DIR_BUILD "${CMAKE_CURRENT_BINARY_DIR}/macros")
set(G4OCCT_MACRO_DIR_INSTALL "${CMAKE_INSTALL_FULL_DATADIR}/g4occt/macros")

# Generate the config header that embeds both paths into the executable.
configure_file(G4OCCTAppConfig.hh.in ${CMAKE_CURRENT_BINARY_DIR}/G4OCCTAppConfig.hh @ONLY)

# Copy macro files into the build-tree macros/ subdirectory.
foreach(_mac init_vis gui init_batch)
  configure_file(macros/${_mac}.mac ${G4OCCT_MACRO_DIR_BUILD}/${_mac}.mac COPYONLY)
endforeach()

install(TARGETS g4occt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  FILES macros/init_vis.mac macros/gui.mac macros/init_batch.mac
  DESTINATION ${CMAKE_INSTALL_DATADIR}/g4occt/macros)

# ── Tests ─────────────────────────────────────────────────────────────────────

if(BUILD_TESTING)
  # Paths to fixture STEP and material files (from the B4c example)
  set(G4OCCT_APP_STEP_DIR "${CMAKE_SOURCE_DIR}/src/examples/B4c/step")
  set(G4OCCT_APP_MAT_XML "${CMAKE_SOURCE_DIR}/src/examples/B4c/materials.xml")

  # Generate a load-test macro with the real file paths substituted in.
  configure_file(macros/load_test.mac.in ${CMAKE_CURRENT_BINARY_DIR}/load_test.mac @ONLY)

  add_test(NAME run_g4occt_empty COMMAND g4occt -m
           ${G4OCCT_MACRO_DIR_BUILD}/init_batch.mac)

  add_test(
    NAME run_g4occt_solid
    COMMAND g4occt ${G4OCCT_APP_STEP_DIR}/absorber.step ${G4OCCT_APP_MAT_XML} -m
            ${G4OCCT_MACRO_DIR_BUILD}/init_batch.mac)

  add_test(
    NAME run_g4occt_cmd_load
    COMMAND g4occt -m ${CMAKE_CURRENT_BINARY_DIR}/load_test.mac)

  foreach(_t run_g4occt_empty run_g4occt_solid run_g4occt_cmd_load)
    set_tests_properties(${_t} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
