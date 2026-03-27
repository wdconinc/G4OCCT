// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTRunAction.hh
/// @brief Run action that manages CSV output via G4AnalysisManager.

#ifndef G4OCCT_APP_G4OCCTRunAction_hh
#define G4OCCT_APP_G4OCCTRunAction_hh

#include <G4Types.hh>
#include <G4UserRunAction.hh>

class G4OCCTOutputConfig;

/**
 * @brief Run action that opens and closes a G4AnalysisManager CSV output file.
 *
 * Three ntuples are booked, each independently toggleable via the shared
 * @c G4OCCTOutputConfig (owned by @c G4OCCTActionInitialization):
 *  - "steps"  → @c <fileName>_nt_steps.csv   (per G4Step)
 *  - "tracks" → @c <fileName>_nt_tracks.csv  (per G4Track, at track end)
 *  - "events" → @c <fileName>_nt_events.csv  (per G4Event, at event end)
 *
 * Output settings are controlled via @c /G4OCCT/output/ messenger commands
 * that write to the shared @c G4OCCTOutputConfig object.  Every run action
 * instance (master and all workers) receives a pointer to the same config and
 * reads it in @c BeginOfRunAction, so UI commands reliably affect all threads.
 */
class G4OCCTRunAction : public G4UserRunAction {
public:
  explicit G4OCCTRunAction(const G4OCCTOutputConfig* config);
  ~G4OCCTRunAction() override = default;

  void BeginOfRunAction(const G4Run* run) override;
  void EndOfRunAction(const G4Run* run) override;

  /// @name Ntuple IDs (valid after BeginOfRunAction, -1 if disabled)
  ///@{
  G4int GetStepsNtupleId() const { return fStepsNtupleId; }
  G4int GetTracksNtupleId() const { return fTracksNtupleId; }
  G4int GetEventsNtupleId() const { return fEventsNtupleId; }
  ///@}

private:
  const G4OCCTOutputConfig* fConfig;

  G4int fStepsNtupleId  = -1;
  G4int fTracksNtupleId = -1;
  G4int fEventsNtupleId = -1;
};

#endif // G4OCCT_APP_G4OCCTRunAction_hh
