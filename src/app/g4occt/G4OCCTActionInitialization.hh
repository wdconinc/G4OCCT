// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTActionInitialization.hh
/// @brief Minimal user action initialisation for the g4occt executable.

#ifndef G4OCCT_APP_G4OCCTActionInitialization_hh
#define G4OCCT_APP_G4OCCTActionInitialization_hh

#include <G4VUserActionInitialization.hh>

/**
 * @brief Minimal action initialisation for the @c g4occt interactive tool.
 *
 * Installs a single-particle gun as the primary generator on each worker
 * thread.  No run, event, or stepping actions are registered by default;
 * the user controls simulation behaviour through Geant4 macro commands.
 */
class G4OCCTActionInitialization : public G4VUserActionInitialization {
public:
  G4OCCTActionInitialization()           = default;
  ~G4OCCTActionInitialization() override = default;

  void BuildForMaster() const override {}
  void Build() const override;
};

#endif // G4OCCT_APP_G4OCCTActionInitialization_hh
