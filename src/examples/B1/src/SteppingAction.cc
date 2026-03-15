// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file SteppingAction.cc
/// @brief Implementation of the B1 stepping action.

#include "SteppingAction.hh"

#include "DetectorConstruction.hh"
#include "EventAction.hh"

#include <G4LogicalVolume.hh>
#include <G4RunManager.hh>
#include <G4Step.hh>
#include <G4TouchableHandle.hh>
#include <G4VPhysicalVolume.hh>

SteppingAction::SteppingAction(EventAction* eventAction) : fEventAction(eventAction) {}

void SteppingAction::UserSteppingAction(const G4Step* step) {
  // Retrieve the scoring volume pointer on first call.
  if (fScoringVolume == nullptr) {
    const auto* detectorConstruction = dynamic_cast<const DetectorConstruction*>(
        G4RunManager::GetRunManager()->GetUserDetectorConstruction());
    if (detectorConstruction != nullptr) {
      fScoringVolume = detectorConstruction->GetScoringVolume();
    }
  }

  // Collect energy deposited only inside shape2.
  G4VPhysicalVolume* pv =
      step->GetPreStepPoint()->GetTouchableHandle()->GetVolume();
  if (pv == nullptr) {
    return;
  }

  G4LogicalVolume* volume = pv->GetLogicalVolume();
  if (volume != fScoringVolume) {
    return;
  }

  fEventAction->AddEdep(step->GetTotalEnergyDeposit());
}
