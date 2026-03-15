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
/// @file EventAction.hh
/// @brief Event-level action for the G4OCCT B1 example.

#ifndef B1_EventAction_hh
#define B1_EventAction_hh

#include <G4UserEventAction.hh>
#include <globals.hh>

class RunAction;

/**
 * @brief Collects total energy deposited in shape2 per event.
 */
class EventAction : public G4UserEventAction {
public:
  explicit EventAction(RunAction* runAction);
  ~EventAction() override = default;

  void BeginOfEventAction(const G4Event* event) override;
  void EndOfEventAction(const G4Event* event) override;

  /// Accumulate @p edep for the current event.
  void AddEdep(G4double edep) { fEdep += edep; }

private:
  RunAction* fRunAction = nullptr;
  G4double fEdep        = 0.0;
};

#endif // B1_EventAction_hh
