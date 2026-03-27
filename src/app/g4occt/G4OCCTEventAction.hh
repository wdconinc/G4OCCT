// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTEventAction.hh
/// @brief Event action that accumulates per-event totals and fills the events ntuple.

#ifndef G4OCCT_APP_G4OCCTEventAction_hh
#define G4OCCT_APP_G4OCCTEventAction_hh

#include <G4Types.hh>
#include <G4UserEventAction.hh>

class G4OCCTRunAction;

/**
 * @brief Event action that accumulates per-event quantities.
 *
 * G4OCCTSteppingAction and G4OCCTTrackingAction call the @c Add* methods
 * below to accumulate totals.  At the end of each event the accumulated
 * values are written to the "events" ntuple.
 */
class G4OCCTEventAction : public G4UserEventAction {
public:
  explicit G4OCCTEventAction(const G4OCCTRunAction* runAction);
  ~G4OCCTEventAction() override = default;

  void BeginOfEventAction(const G4Event* event) override;
  void EndOfEventAction(const G4Event* event) override;

  /// Returns the current event ID; cached in BeginOfEventAction.
  G4int GetEventId() const { return fEventId; }

  /// Called from G4OCCTSteppingAction for each step.
  void AddStep(G4double edep, G4double stepLength);
  /// Called from G4OCCTTrackingAction at the end of each track.
  void AddTrack();
  /// Called from G4OCCTTrackingAction for primary tracks.
  void IncrementPrimaries();

private:
  const G4OCCTRunAction* fRunAction;

  G4int fEventId        = -1;
  G4double fTotalEdep   = 0.0;
  G4double fTotalLength = 0.0;
  G4int fNTracks        = 0;
  G4int fNSteps         = 0;
  G4int fNPrimaries     = 0;
};

#endif // G4OCCT_APP_G4OCCTEventAction_hh
