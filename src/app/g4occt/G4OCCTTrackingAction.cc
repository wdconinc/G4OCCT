// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTTrackingAction.cc

#include "G4OCCTTrackingAction.hh"
#include "G4OCCTEventAction.hh"
#include "G4OCCTRunAction.hh"

#include <G4AnalysisManager.hh>
#include <G4Event.hh>
#include <G4EventManager.hh>
#include <G4SystemOfUnits.hh>
#include <G4Track.hh>
#include <G4VProcess.hh>

G4OCCTTrackingAction::G4OCCTTrackingAction(G4OCCTEventAction*     eventAction,
                                            const G4OCCTRunAction* runAction)
    : fEventAction(eventAction), fRunAction(runAction) {}

void G4OCCTTrackingAction::PreUserTrackingAction(const G4Track* track) {
  fCurrentTrackEdep = 0.0;
  if (track->GetParentID() == 0) {
    fEventAction->IncrementPrimaries();
  }
}

void G4OCCTTrackingAction::PostUserTrackingAction(const G4Track* track) {
  fEventAction->AddTrack();

  const G4int ntId = fRunAction->GetTracksNtupleId();
  if (ntId < 0) return;

  const G4int eventId =
      G4EventManager::GetEventManager()->GetConstCurrentEvent()->GetEventID();

  const G4ThreeVector& vertex = track->GetVertexPosition();
  const G4ThreeVector& final  = track->GetPosition();

  const G4VProcess* creator = track->GetCreatorProcess();
  const G4String creatorName = creator ? creator->GetProcessName() : "";

  auto* am  = G4AnalysisManager::Instance();
  G4int col = 0;
  am->FillNtupleIColumn(ntId, col++, eventId);
  am->FillNtupleIColumn(ntId, col++, track->GetTrackID());
  am->FillNtupleIColumn(ntId, col++, track->GetParentID());
  am->FillNtupleSColumn(ntId, col++, track->GetDefinition()->GetParticleName());
  am->FillNtupleSColumn(ntId, col++, creatorName);
  am->FillNtupleDColumn(ntId, col++, fCurrentTrackEdep / MeV);
  am->FillNtupleDColumn(ntId, col++, track->GetTrackLength() / mm);
  am->FillNtupleDColumn(ntId, col++, vertex.x() / mm);
  am->FillNtupleDColumn(ntId, col++, vertex.y() / mm);
  am->FillNtupleDColumn(ntId, col++, vertex.z() / mm);
  am->FillNtupleDColumn(ntId, col++, final.x() / mm);
  am->FillNtupleDColumn(ntId, col++, final.y() / mm);
  am->FillNtupleDColumn(ntId, col++, final.z() / mm);
  am->AddNtupleRow(ntId);
}

void G4OCCTTrackingAction::AddEdepToCurrentTrack(G4double edep) {
  fCurrentTrackEdep += edep;
}
