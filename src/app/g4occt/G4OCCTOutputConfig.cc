// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTOutputConfig.cc

#include "G4OCCTOutputConfig.hh"

#include <G4GenericMessenger.hh>

G4OCCTOutputConfig::G4OCCTOutputConfig() {
  fMessenger = std::make_unique<G4GenericMessenger>(this, "/G4OCCT/output/",
                                                    "G4OCCT output control");

  fMessenger->DeclareProperty("setFileName", fileName)
      .SetGuidance("Base filename for CSV output (without extension).")
      .SetParameterName("name", false)
      .SetDefaultValue("g4occt");

  fMessenger->DeclareProperty("recordSteps", recordSteps)
      .SetGuidance("Enable or disable per-step ntuple (-> <name>_nt_steps.csv).")
      .SetDefaultValue("true");

  fMessenger->DeclareProperty("recordTracks", recordTracks)
      .SetGuidance("Enable or disable per-track ntuple (-> <name>_nt_tracks.csv).")
      .SetDefaultValue("true");

  fMessenger->DeclareProperty("recordEvents", recordEvents)
      .SetGuidance("Enable or disable per-event ntuple (-> <name>_nt_events.csv).")
      .SetDefaultValue("true");
}

G4OCCTOutputConfig::~G4OCCTOutputConfig() = default;
