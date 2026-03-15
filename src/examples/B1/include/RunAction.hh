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
/// @file RunAction.hh
/// @brief Run-level action for the G4OCCT B1 example.

#ifndef B1_RunAction_hh
#define B1_RunAction_hh

#include <G4Accumulable.hh>
#include <G4UserRunAction.hh>

class G4Run;

/**
 * @brief Accumulates total energy deposited in shape2 across the run.
 *
 * At the end of the run, prints the total and per-event energy deposit in
 * shape2.
 */
class RunAction : public G4UserRunAction {
 public:
  RunAction();
  ~RunAction() override = default;

  void BeginOfRunAction(const G4Run* run) override;
  void EndOfRunAction(const G4Run* run) override;

  /// Add @p edep to the accumulated energy deposit.
  void AddEdep(G4double edep);

 private:
  G4Accumulable<G4double> fEdep{0.0};
  G4Accumulable<G4double> fEdep2{0.0};
};

#endif  // B1_RunAction_hh
