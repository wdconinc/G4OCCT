// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file SteppingAction.hh
/// @brief Stepping action for the G4OCCT B1 example.

#ifndef B1_SteppingAction_hh
#define B1_SteppingAction_hh

#include <G4UserSteppingAction.hh>

class EventAction;
class G4LogicalVolume;

/**
 * @brief Accumulates energy deposited inside the scoring volume (shape2).
 */
class SteppingAction : public G4UserSteppingAction {
 public:
  explicit SteppingAction(EventAction* eventAction);
  ~SteppingAction() override = default;

  void UserSteppingAction(const G4Step* step) override;

 private:
  EventAction* fEventAction = nullptr;
  G4LogicalVolume* fScoringVolume = nullptr;
};

#endif  // B1_SteppingAction_hh
