# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

add_executable(
  g4occt
  main.cc
  G4OCCTDetectorConstruction.cc
  G4OCCTActionInitialization.cc
  G4OCCTRunAction.cc
  G4OCCTEventAction.cc
  G4OCCTSteppingAction.cc
  G4OCCTTrackingAction.cc)

target_include_directories(g4occt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(g4occt PRIVATE G4OCCT::G4OCCT ${Geant4_LIBRARIES})

# ── Macro data directory (FHS: share/g4occt/macros) ──────────────────────────

# Per the Linux FHS, architecture-independent application data lives under
# ${CMAKE_INSTALL_DATADIR}/<package>.  The build-tree copy goes into
# ${CMAKE_CURRENT_BINARY_DIR}/macros/ so the layout mirrors the install tree.

set(G4OCCT_MACRO_DIR_BUILD "${CMAKE_CURRENT_BINARY_DIR}/macros")
set(G4OCCT_MACRO_DIR_INSTALL "${CMAKE_INSTALL_FULL_DATADIR}/g4occt/macros")

# Embed both directories directly as compile definitions — no generated header.
target_compile_definitions(
  g4occt
  PRIVATE G4OCCT_MACRO_DIR_INSTALL="${G4OCCT_MACRO_DIR_INSTALL}"
          G4OCCT_MACRO_DIR_BUILD="${G4OCCT_MACRO_DIR_BUILD}")

# Copy macro files into the build-tree macros/ subdirectory.
foreach(_mac init_vis gui init_batch run_output)
  configure_file(macros/${_mac}.mac ${G4OCCT_MACRO_DIR_BUILD}/${_mac}.mac COPYONLY)
endforeach()

install(TARGETS g4occt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  FILES macros/init_vis.mac macros/gui.mac macros/init_batch.mac macros/run_output.mac
  DESTINATION ${CMAKE_INSTALL_DATADIR}/g4occt/macros)

# ── Tests ─────────────────────────────────────────────────────────────────────

if(BUILD_TESTING)
  # Paths to fixture STEP and material files (from the B4c example)
  set(G4OCCT_APP_STEP_DIR "${CMAKE_SOURCE_DIR}/src/examples/B4c/step")
  set(G4OCCT_APP_MAT_XML "${CMAKE_SOURCE_DIR}/src/examples/B4c/materials.xml")

  # Generate a load-test macro with the real file paths substituted in.
  configure_file(macros/load_test.mac.in ${CMAKE_CURRENT_BINARY_DIR}/load_test.mac @ONLY)

  add_test(NAME run_g4occt_empty COMMAND $<TARGET_FILE:g4occt> -m
           ${CMAKE_CURRENT_BINARY_DIR}/init_batch.mac)

  add_test(
    NAME run_g4occt_solid
    COMMAND $<TARGET_FILE:g4occt> ${G4OCCT_APP_STEP_DIR}/absorber.step ${G4OCCT_APP_MAT_XML} -m
            ${CMAKE_CURRENT_BINARY_DIR}/init_batch.mac)

  add_test(
    NAME run_g4occt_cmd_load
    COMMAND $<TARGET_FILE:g4occt> -m ${CMAKE_CURRENT_BINARY_DIR}/load_test.mac)

  # Output test: run 10 events and verify all three CSV files are produced.
  # No STEP file is loaded: the test only exercises the output mechanism
  # (G4AnalysisManager CSV ntuples), not the STEP geometry import.  Loading a
  # STEP solid triggers a pre-existing navigation heap-corruption when the
  # process is started via fork+exec (e.g. under CTest), which is a separate
  # G4OCCTSolid issue.  The run_g4occt_solid test already covers STEP loading.
  # Use -t 1 to limit to one worker thread: the CSV backend writes per-thread
  # files (e.g. g4occt_nt_steps_t0.csv) and does not support ntuple merging.
  set(_output_dir ${CMAKE_CURRENT_BINARY_DIR}/output_test)
  add_test(
    NAME run_g4occt_output
    COMMAND g4occt -t 1 -m ${G4OCCT_MACRO_DIR_BUILD}/run_output.mac
    WORKING_DIRECTORY ${_output_dir})

  # Geant4 physics data must be accessible when running events.  Collect all
  # dataset environment variables from Geant4Config.cmake and pass them to the
  # test so CTest works without sourcing geant4.sh first.
  set(_g4_env_vars "")
  foreach(_ds ${Geant4_DATASETS})
    list(APPEND _g4_env_vars
         "${Geant4_DATASET_${_ds}_ENVVAR}=${Geant4_DATASET_${_ds}_PATH}")
  endforeach()

  set_tests_properties(
    run_g4occt_output
    PROPERTIES
      TIMEOUT 120
      FIXTURES_SETUP g4occt_output_files
      ENVIRONMENT "${_g4_env_vars}")

  # Verify each CSV is non-empty.  With one worker thread the CSV backend
  # writes files with a _t0 suffix (e.g. g4occt_nt_steps_t0.csv).
  foreach(_level steps tracks events)
    add_test(
      NAME check_g4occt_output_${_level}
      COMMAND ${CMAKE_COMMAND} -E cat ${_output_dir}/g4occt_nt_${_level}_t0.csv)
    set_tests_properties(
      check_g4occt_output_${_level}
      PROPERTIES
        TIMEOUT 10
        FIXTURES_REQUIRED g4occt_output_files)
  endforeach()

  # Create the output directory at configure time so WORKING_DIRECTORY exists.
  file(MAKE_DIRECTORY ${_output_dir})

  foreach(_t run_g4occt_empty run_g4occt_solid run_g4occt_cmd_load)
    set_tests_properties(${_t} PROPERTIES TIMEOUT 120 WORKING_DIRECTORY
                         ${CMAKE_CURRENT_BINARY_DIR})
  endforeach()
endif()
