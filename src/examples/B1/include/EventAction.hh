// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

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
  G4double fEdep = 0.0;
};

#endif  // B1_EventAction_hh
