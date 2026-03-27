// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSteppingAction.cc

#include "G4OCCTSteppingAction.hh"
#include "G4OCCTEventAction.hh"
#include "G4OCCTRunAction.hh"
#include "G4OCCTTrackingAction.hh"

#include <G4AnalysisManager.hh>
#include <G4Event.hh>
#include <G4EventManager.hh>
#include <G4Step.hh>
#include <G4SystemOfUnits.hh>
#include <G4Track.hh>
#include <G4VPhysicalVolume.hh>

G4OCCTSteppingAction::G4OCCTSteppingAction(G4OCCTEventAction*     eventAction,
                                            G4OCCTTrackingAction*  trackingAction,
                                            const G4OCCTRunAction* runAction)
    : fEventAction(eventAction), fTrackingAction(trackingAction), fRunAction(runAction) {}

void G4OCCTSteppingAction::UserSteppingAction(const G4Step* step) {
  const G4double edep       = step->GetTotalEnergyDeposit();
  const G4double stepLength = step->GetStepLength();

  fEventAction->AddStep(edep, stepLength);
  fTrackingAction->AddEdepToCurrentTrack(edep);

  const G4int ntId = fRunAction->GetStepsNtupleId();
  if (ntId < 0) return;

  const G4Track*           track = step->GetTrack();
  const G4StepPoint*       pre   = step->GetPreStepPoint();
  const G4ThreeVector&     pos   = pre->GetPosition();
  const G4VPhysicalVolume* vol   = pre->GetPhysicalVolume();
  const G4String volName = vol ? vol->GetLogicalVolume()->GetName() : "OutOfWorld";
  const G4int eventId =
      G4EventManager::GetEventManager()->GetConstCurrentEvent()->GetEventID();

  auto* am  = G4AnalysisManager::Instance();
  G4int col = 0;
  am->FillNtupleIColumn(ntId, col++, eventId);
  am->FillNtupleIColumn(ntId, col++, track->GetTrackID());
  am->FillNtupleIColumn(ntId, col++, track->GetCurrentStepNumber());
  am->FillNtupleSColumn(ntId, col++, volName);
  am->FillNtupleSColumn(ntId, col++, track->GetDefinition()->GetParticleName());
  am->FillNtupleDColumn(ntId, col++, edep / MeV);
  am->FillNtupleDColumn(ntId, col++, stepLength / mm);
  am->FillNtupleDColumn(ntId, col++, pos.x() / mm);
  am->FillNtupleDColumn(ntId, col++, pos.y() / mm);
  am->FillNtupleDColumn(ntId, col++, pos.z() / mm);
  am->AddNtupleRow(ntId);
}
