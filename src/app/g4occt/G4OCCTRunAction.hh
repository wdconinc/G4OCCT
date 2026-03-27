// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTRunAction.hh
/// @brief Run action that manages CSV output via G4AnalysisManager.

#ifndef G4OCCT_APP_G4OCCTRunAction_hh
#define G4OCCT_APP_G4OCCTRunAction_hh

#include <G4String.hh>
#include <G4Types.hh>
#include <G4UserRunAction.hh>

#include <memory>

class G4GenericMessenger;

/**
 * @brief Run action that opens and closes a G4AnalysisManager CSV output file.
 *
 * Three ntuples are booked, each independently toggleable via messenger:
 *  - "steps"  → @c <fileName>_nt_steps.csv   (per G4Step)
 *  - "tracks" → @c <fileName>_nt_tracks.csv  (per G4Track, at track end)
 *  - "events" → @c <fileName>_nt_events.csv  (per G4Event, at event end)
 *
 * Messenger commands (all under @c /G4OCCT/output/):
 *  - @c setFileName   — base filename without extension (default: @c "g4occt")
 *  - @c recordSteps   — enable/disable steps ntuple   (default: @c true)
 *  - @c recordTracks  — enable/disable tracks ntuple  (default: @c true)
 *  - @c recordEvents  — enable/disable events ntuple  (default: @c true)
 */
class G4OCCTRunAction : public G4UserRunAction {
public:
  /// @param isMaster  When @c true the messenger is created (master thread only).
  explicit G4OCCTRunAction(G4bool isMaster = false);
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
  void DefineMessenger();

  G4String fFileName     = "g4occt";
  G4bool fRecordSteps   = true;
  G4bool fRecordTracks  = true;
  G4bool fRecordEvents  = true;

  G4int fStepsNtupleId  = -1;
  G4int fTracksNtupleId = -1;
  G4int fEventsNtupleId = -1;

  std::unique_ptr<G4GenericMessenger> fMessenger;
};

#endif // G4OCCT_APP_G4OCCTRunAction_hh
