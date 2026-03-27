// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTOutputConfig.hh
/// @brief Shared output configuration for the g4occt CSV output mechanism.

#ifndef G4OCCT_APP_G4OCCTOutputConfig_hh
#define G4OCCT_APP_G4OCCTOutputConfig_hh

#include <G4String.hh>
#include <G4Types.hh>

#include <memory>

class G4GenericMessenger;

/**
 * @brief Shared output configuration used by all G4OCCTRunAction instances.
 *
 * Owns the @c /G4OCCT/output/ messenger so that UI commands issued before the
 * run affect every worker thread's run action (which read these values in
 * @c BeginOfRunAction).  A single instance lives in
 * @c G4OCCTActionInitialization and is passed by pointer to every run action,
 * so the messenger is registered in both MT and sequential builds.
 *
 * @note The messenger binds directly to the public data members, which are
 *       written on the UI thread before @c BeamOn and read by worker threads
 *       only during @c BeginOfRunAction — no locking is required.
 */
class G4OCCTOutputConfig {
public:
  G4OCCTOutputConfig();
  ~G4OCCTOutputConfig();

  G4String fileName   = "g4occt"; ///< Base CSV filename (no extension).
  G4bool recordSteps  = true;     ///< Enable the per-step ntuple.
  G4bool recordTracks = true;     ///< Enable the per-track ntuple.
  G4bool recordEvents = true;     ///< Enable the per-event ntuple.

private:
  std::unique_ptr<G4GenericMessenger> fMessenger;
};

#endif // G4OCCT_APP_G4OCCTOutputConfig_hh
