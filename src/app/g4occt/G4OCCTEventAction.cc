// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTEventAction.cc

#include "G4OCCTEventAction.hh"
#include "G4OCCTRunAction.hh"

#include <G4AnalysisManager.hh>
#include <G4Event.hh>
#include <G4SystemOfUnits.hh>

G4OCCTEventAction::G4OCCTEventAction(const G4OCCTRunAction* runAction)
    : fRunAction(runAction) {}

void G4OCCTEventAction::BeginOfEventAction(const G4Event* event) {
  fEventId     = event->GetEventID();
  fTotalEdep   = 0.0;
  fTotalLength = 0.0;
  fNTracks     = 0;
  fNSteps      = 0;
  fNPrimaries  = 0;
}

void G4OCCTEventAction::EndOfEventAction(const G4Event* event) {
  const G4int ntId = fRunAction->GetEventsNtupleId();
  if (ntId < 0) return;

  auto* am  = G4AnalysisManager::Instance();
  G4int col = 0;
  am->FillNtupleIColumn(ntId, col++, event->GetEventID());
  am->FillNtupleDColumn(ntId, col++, fTotalEdep / MeV);
  am->FillNtupleDColumn(ntId, col++, fTotalLength / mm);
  am->FillNtupleIColumn(ntId, col++, fNPrimaries);
  am->FillNtupleIColumn(ntId, col++, fNTracks);
  am->FillNtupleIColumn(ntId, col++, fNSteps);
  am->AddNtupleRow(ntId);
}

void G4OCCTEventAction::AddStep(G4double edep, G4double stepLength) {
  fTotalEdep += edep;
  fTotalLength += stepLength;
  ++fNSteps;
}

void G4OCCTEventAction::AddTrack() { ++fNTracks; }

void G4OCCTEventAction::IncrementPrimaries() { ++fNPrimaries; }
