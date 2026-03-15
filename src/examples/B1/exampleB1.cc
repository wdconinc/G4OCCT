//
// ********************************************************************
// * License and Disclaimer                                           *
// *                                                                  *
// * The  Geant4 software  is  copyright of the Copyright Holders  of *
// * the Geant4 Collaboration.  It is provided  under  the terms  and *
// * conditions of the Geant4 Software License,  included in the file *
// * LICENSE and available at  http://cern.ch/geant4/license .  These *
// * include a list of copyright holders.                             *
// *                                                                  *
// * Neither the authors of this software system, nor their employing *
// * institutes,nor the agencies providing financial support for this *
// * work  make  any representation or  warranty, express or implied, *
// * regarding  this  software system or assume any liability for its *
// * use.  Please see the license in the file  LICENSE  and URL above *
// * for the full disclaimer and the limitation of liability.         *
// *                                                                  *
// * This  code  implementation is the result of  the  scientific and *
// * technical work of the GEANT4 collaboration.                      *
// * By using,  copying,  modifying or  distributing the software (or *
// * any work based  on the software)  you  agree  to acknowledge its *
// * use  in  resulting  scientific  publications,  and indicate your *
// * acceptance of all terms of the Geant4 Software license.          *
// ********************************************************************
//
/// @file exampleB1.cc
/// @brief G4OCCT version of the Geant4 basic/B1 example.
///
/// Demonstrates loading CAD geometry from STEP files via G4OCCTSolid and
/// running a simple 6 MeV gamma simulation in a water phantom.  An envelope
/// box of G4_WATER contains two shapes whose boundaries are defined by STEP
/// files and whose materials are specified using G4NistManager names.
///
/// Usage (batch):
///   ./exampleB1 run.mac [nthreads]
///
/// The @p run.mac macro initialises the run manager and fires the beam.
/// An optional second argument @p nthreads (positive integer) sets the number
/// of worker threads before /run/initialize so that multi-threaded behaviour
/// can be exercised (e.g. under ThreadSanitizer).

#include "ActionInitialization.hh"
#include "DetectorConstruction.hh"

#include <G4RunManagerFactory.hh>
#include <G4UImanager.hh>
#include <QBBC.hh>

#include <cerrno>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
  // ── Run manager ───────────────────────────────────────────────────────────

  auto* runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);

  // ── User initializations ──────────────────────────────────────────────────

  runManager->SetUserInitialization(new B1::DetectorConstruction());
  runManager->SetUserInitialization(new QBBC());
  runManager->SetUserInitialization(new ActionInitialization());

  // ── UI manager ────────────────────────────────────────────────────────────

  G4UImanager* UImanager = G4UImanager::GetUIpointer();

  // Optional second argument: number of worker threads (must be set before
  // /run/initialize, which is the first command in the macro).
  if (argc > 2) {
    char* endptr  = nullptr;
    errno         = 0;
    long nthreads = std::strtol(argv[2], &endptr, 10);
    if (errno != 0 || endptr == argv[2] || *endptr != '\0' || nthreads <= 0) {
      std::cerr << "exampleB1: invalid nthreads argument '" << argv[2]
                << "' (must be a positive integer)\n";
      return 1;
    }
    int rc = UImanager->ApplyCommand(G4String("/run/numberOfThreads ") + G4String(argv[2]));
    if (rc != 0) {
      std::cerr << "exampleB1: /run/numberOfThreads command failed (code " << rc << ")\n";
      return 1;
    }
  }

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
