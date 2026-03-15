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
    const auto* detectorConstruction = dynamic_cast<const B1::DetectorConstruction*>(
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
