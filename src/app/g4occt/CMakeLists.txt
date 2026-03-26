# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

add_executable(
  g4occt
  main.cc
  G4OCCTDetectorConstruction.cc
  G4OCCTActionInitialization.cc)

target_include_directories(g4occt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(g4occt PRIVATE G4OCCT::G4OCCT ${Geant4_LIBRARIES})

# Copy macro files next to the executable so they can be found at runtime.
foreach(_mac init_vis gui init_batch)
  configure_file(macros/${_mac}.mac ${CMAKE_CURRENT_BINARY_DIR}/${_mac}.mac COPYONLY)
endforeach()

install(TARGETS g4occt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  FILES macros/init_vis.mac macros/gui.mac macros/init_batch.mac
  DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Tests ─────────────────────────────────────────────────────────────────────

if(BUILD_TESTING)
  # Paths to fixture STEP and material files (from the B4c example)
  set(G4OCCT_APP_STEP_DIR "${CMAKE_SOURCE_DIR}/src/examples/B4c/step")
  set(G4OCCT_APP_MAT_XML "${CMAKE_SOURCE_DIR}/src/examples/B4c/materials.xml")

  # Generate a load-test macro with the real file paths substituted in.
  configure_file(macros/load_test.mac.in ${CMAKE_CURRENT_BINARY_DIR}/load_test.mac @ONLY)

  add_test(NAME run_g4occt_empty COMMAND g4occt -m
           ${CMAKE_CURRENT_BINARY_DIR}/init_batch.mac)

  add_test(
    NAME run_g4occt_solid
    COMMAND g4occt ${G4OCCT_APP_STEP_DIR}/absorber.step ${G4OCCT_APP_MAT_XML} -m
            ${CMAKE_CURRENT_BINARY_DIR}/init_batch.mac)

  add_test(
    NAME run_g4occt_cmd_load
    COMMAND g4occt -m ${CMAKE_CURRENT_BINARY_DIR}/load_test.mac)

  foreach(_t run_g4occt_empty run_g4occt_solid run_g4occt_cmd_load)
    set_tests_properties(${_t} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
