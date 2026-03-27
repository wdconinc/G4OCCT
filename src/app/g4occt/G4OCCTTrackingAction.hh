// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTTrackingAction.hh
/// @brief Tracking action that fills the per-track ntuple.

#ifndef G4OCCT_APP_G4OCCTTrackingAction_hh
#define G4OCCT_APP_G4OCCTTrackingAction_hh

#include <G4Types.hh>
#include <G4UserTrackingAction.hh>

class G4OCCTEventAction;
class G4OCCTRunAction;

/**
 * @brief Tracking action that records one row per G4Track into the "tracks" ntuple.
 *
 * Columns written per track (at PostUserTrackingAction):
 *  EventID, TrackID, ParentID, ParticleName, CreatorProcess,
 *  TotalEdep [MeV], TrackLength [mm],
 *  X0 [mm], Y0 [mm], Z0 [mm], Xf [mm], Yf [mm], Zf [mm]
 *
 * TotalEdep is accumulated step-by-step via AddEdepToCurrentTrack(), called
 * from G4OCCTSteppingAction.
 */
class G4OCCTTrackingAction : public G4UserTrackingAction {
public:
  G4OCCTTrackingAction(G4OCCTEventAction* eventAction, const G4OCCTRunAction* runAction);
  ~G4OCCTTrackingAction() override = default;

  void PreUserTrackingAction(const G4Track* track) override;
  void PostUserTrackingAction(const G4Track* track) override;

  /// Called from G4OCCTSteppingAction to accumulate energy deposit for the current track.
  void AddEdepToCurrentTrack(G4double edep);

private:
  G4OCCTEventAction* fEventAction;
  const G4OCCTRunAction* fRunAction;

  G4double fCurrentTrackEdep = 0.0;
};

#endif // G4OCCT_APP_G4OCCTTrackingAction_hh
