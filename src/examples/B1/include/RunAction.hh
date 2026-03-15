// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

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
