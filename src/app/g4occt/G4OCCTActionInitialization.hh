// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTActionInitialization.hh
/// @brief Minimal user action initialisation for the g4occt executable.

#ifndef G4OCCT_APP_G4OCCTActionInitialization_hh
#define G4OCCT_APP_G4OCCTActionInitialization_hh

#include <G4VUserActionInitialization.hh>

/**
 * @brief Action initialisation for the @c g4occt interactive tool.
 *
 * Installs a 1 GeV proton gun, a run action (with CSV output), an event
 * action, a tracking action, and a stepping action on each worker thread.
 * The master run action owns the @c /G4OCCT/output/ messenger.
 */
class G4OCCTActionInitialization : public G4VUserActionInitialization {
public:
  G4OCCTActionInitialization()           = default;
  ~G4OCCTActionInitialization() override = default;

  void BuildForMaster() const override;
  void Build() const override;
};

#endif // G4OCCT_APP_G4OCCTActionInitialization_hh
