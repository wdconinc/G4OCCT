// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file exampleB1.cc
/// @brief G4OCCT version of the Geant4 basic/B1 example.
///
/// Demonstrates loading CAD geometry from STEP files via G4OCCTSolid and
/// running a simple 6 MeV gamma simulation in a water phantom.  An envelope
/// box of G4_WATER contains two shapes whose boundaries are defined by STEP
/// files and whose materials are specified using G4NistManager names.
///
/// Usage (batch):
///   ./exampleB1 run.mac
///
/// The @p run.mac macro initialises the run manager and fires the beam.

#include "ActionInitialization.hh"
#include "DetectorConstruction.hh"

#include <G4RunManagerFactory.hh>
#include <G4UImanager.hh>
#include <QBBC.hh>

int main(int argc, char** argv) {
  // ── Run manager ───────────────────────────────────────────────────────────

  auto* runManager =
      G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);

  // ── User initializations ──────────────────────────────────────────────────

  runManager->SetUserInitialization(new DetectorConstruction());
  runManager->SetUserInitialization(new QBBC());
  runManager->SetUserInitialization(new ActionInitialization());

  // ── UI manager ────────────────────────────────────────────────────────────

  G4UImanager* UImanager = G4UImanager::GetUIpointer();

  if (argc > 1) {
    // Batch mode: execute the supplied macro file.
    G4String command  = "/control/execute ";
    G4String fileName = argv[1];
    UImanager->ApplyCommand(command + fileName);
  } else {
    // Minimal headless default: initialise and exit without running any events.
    UImanager->ApplyCommand("/run/initialize");
  }

  delete runManager;
  return 0;
}
