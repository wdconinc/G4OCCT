// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSteppingAction.hh
/// @brief Stepping action that fills the per-step ntuple.

#ifndef G4OCCT_APP_G4OCCTSteppingAction_hh
#define G4OCCT_APP_G4OCCTSteppingAction_hh

#include <G4UserSteppingAction.hh>

class G4OCCTEventAction;
class G4OCCTRunAction;
class G4OCCTTrackingAction;

/**
 * @brief Stepping action that records one row per G4Step into the "steps" ntuple.
 *
 * Columns written per step:
 *  EventID, TrackID, StepNo, VolumeName, ParticleName,
 *  Edep [MeV], StepLength [mm], X [mm], Y [mm], Z [mm]
 *
 * Also notifies G4OCCTEventAction (total Edep and step length) and
 * G4OCCTTrackingAction (per-track Edep accumulation).
 */
class G4OCCTSteppingAction : public G4UserSteppingAction {
public:
  G4OCCTSteppingAction(G4OCCTEventAction* eventAction, G4OCCTTrackingAction* trackingAction,
                       const G4OCCTRunAction* runAction);
  ~G4OCCTSteppingAction() override = default;

  void UserSteppingAction(const G4Step* step) override;

private:
  G4OCCTEventAction* fEventAction;
  G4OCCTTrackingAction* fTrackingAction;
  const G4OCCTRunAction* fRunAction;
};

#endif // G4OCCT_APP_G4OCCTSteppingAction_hh
