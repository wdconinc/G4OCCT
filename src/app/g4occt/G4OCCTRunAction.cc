// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTRunAction.cc

#include "G4OCCTRunAction.hh"
#include "G4OCCTOutputConfig.hh"

#include <G4AnalysisManager.hh>
#include <G4Run.hh>
#include <G4SystemOfUnits.hh>
#include <G4Threading.hh>

G4OCCTRunAction::G4OCCTRunAction(const G4OCCTOutputConfig* config) : fConfig(config) {}

void G4OCCTRunAction::BeginOfRunAction(const G4Run*) {
  auto* am = G4AnalysisManager::Instance();
  am->SetDefaultFileType("csv");
  am->SetVerboseLevel(0);
  // In MT mode the CSV backend does not support per-thread merging; each
  // worker writes its own suffixed file (e.g. g4occt_nt_steps_t0.csv).
  // Disable the merge attempt to avoid a crash during CloseFile().
  // SetNtupleMerging is only meaningful and safe in multithreaded mode.
  if (G4Threading::IsMultithreadedApplication()) {
    am->SetNtupleMerging(false);
  }
  am->OpenFile(fConfig->fileName);

  fStepsNtupleId  = -1;
  fTracksNtupleId = -1;
  fEventsNtupleId = -1;

  if (fConfig->recordSteps) {
    fStepsNtupleId = am->CreateNtuple("steps", "Per-step data");
    am->CreateNtupleIColumn(fStepsNtupleId, "EventID");
    am->CreateNtupleIColumn(fStepsNtupleId, "TrackID");
    am->CreateNtupleIColumn(fStepsNtupleId, "StepNo");
    am->CreateNtupleSColumn(fStepsNtupleId, "VolumeName");
    am->CreateNtupleSColumn(fStepsNtupleId, "ParticleName");
    am->CreateNtupleDColumn(fStepsNtupleId, "Edep");       // MeV
    am->CreateNtupleDColumn(fStepsNtupleId, "StepLength"); // mm
    am->CreateNtupleDColumn(fStepsNtupleId, "X");          // mm
    am->CreateNtupleDColumn(fStepsNtupleId, "Y");          // mm
    am->CreateNtupleDColumn(fStepsNtupleId, "Z");          // mm
    am->FinishNtuple(fStepsNtupleId);
  }

  if (fConfig->recordTracks) {
    fTracksNtupleId = am->CreateNtuple("tracks", "Per-track data");
    am->CreateNtupleIColumn(fTracksNtupleId, "EventID");
    am->CreateNtupleIColumn(fTracksNtupleId, "TrackID");
    am->CreateNtupleIColumn(fTracksNtupleId, "ParentID");
    am->CreateNtupleSColumn(fTracksNtupleId, "ParticleName");
    am->CreateNtupleSColumn(fTracksNtupleId, "CreatorProcess");
    am->CreateNtupleDColumn(fTracksNtupleId, "TotalEdep");   // MeV
    am->CreateNtupleDColumn(fTracksNtupleId, "TrackLength"); // mm
    am->CreateNtupleDColumn(fTracksNtupleId, "X0");          // mm
    am->CreateNtupleDColumn(fTracksNtupleId, "Y0");          // mm
    am->CreateNtupleDColumn(fTracksNtupleId, "Z0");          // mm
    am->CreateNtupleDColumn(fTracksNtupleId, "Xf");          // mm
    am->CreateNtupleDColumn(fTracksNtupleId, "Yf");          // mm
    am->CreateNtupleDColumn(fTracksNtupleId, "Zf");          // mm
    am->FinishNtuple(fTracksNtupleId);
  }

  if (fConfig->recordEvents) {
    fEventsNtupleId = am->CreateNtuple("events", "Per-event data");
    am->CreateNtupleIColumn(fEventsNtupleId, "EventID");
    am->CreateNtupleDColumn(fEventsNtupleId, "TotalEdep");        // MeV
    am->CreateNtupleDColumn(fEventsNtupleId, "TotalTrackLength"); // mm
    am->CreateNtupleIColumn(fEventsNtupleId, "NPrimaries");
    am->CreateNtupleIColumn(fEventsNtupleId, "NTracks");
    am->CreateNtupleIColumn(fEventsNtupleId, "NSteps");
    am->FinishNtuple(fEventsNtupleId);
  }
}

void G4OCCTRunAction::EndOfRunAction(const G4Run*) {
  auto* am = G4AnalysisManager::Instance();
  am->Write();
  am->CloseFile();
}
