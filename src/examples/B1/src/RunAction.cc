// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file RunAction.cc
/// @brief Implementation of the B1 run-level action.

#include "RunAction.hh"

#include <G4AccumulableManager.hh>
#include <G4Run.hh>
#include <G4RunManager.hh>
#include <G4SystemOfUnits.hh>
#include <G4UnitsTable.hh>
#include <G4ios.hh>

#include <cmath>

RunAction::RunAction() {
  G4AccumulableManager* accumulableManager = G4AccumulableManager::Instance();
  accumulableManager->RegisterAccumulable(fEdep);
  accumulableManager->RegisterAccumulable(fEdep2);
}

void RunAction::BeginOfRunAction(const G4Run* /* run */) {
  G4AccumulableManager::Instance()->Reset();
}

void RunAction::EndOfRunAction(const G4Run* run) {
  G4int nEvents = run->GetNumberOfEvent();
  if (nEvents == 0) {
    return;
  }

  G4AccumulableManager::Instance()->Merge();

  const G4double edep     = fEdep.GetValue();
  const G4double edep2    = fEdep2.GetValue();
  const G4double rms      = edep2 - edep * edep / nEvents;
  const G4double rmsFinal = (rms > 0.0) ? std::sqrt(rms) : 0.0;

  G4cout << "\n------------------------------------------------------------\n"
         << " The run consists of " << nEvents << " events\n"
         << " Cumulated dose in scoring volume (shape2):\n"
         << "   Total edep  : " << G4BestUnit(edep, "Energy") << "\n"
         << "   RMS         : " << G4BestUnit(rmsFinal, "Energy") << "\n"
         << "------------------------------------------------------------\n"
         << G4endl;
}

void RunAction::AddEdep(G4double edep) {
  fEdep  += edep;
  fEdep2 += edep * edep;
}
