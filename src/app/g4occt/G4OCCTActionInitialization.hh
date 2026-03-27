// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTActionInitialization.hh
/// @brief Minimal user action initialisation for the g4occt executable.

#ifndef G4OCCT_APP_G4OCCTActionInitialization_hh
#define G4OCCT_APP_G4OCCTActionInitialization_hh

#include "G4OCCTOutputConfig.hh"

#include <G4VUserActionInitialization.hh>

/**
 * @brief Action initialisation for the @c g4occt interactive tool.
 *
 * Owns a single @c G4OCCTOutputConfig that carries the @c /G4OCCT/output/
 * messenger and the shared output settings.  Both the master and every worker
 * run action receive a pointer to this config so that UI commands affect all
 * threads in MT builds and are available in sequential builds too.
 *
 * Installs a 1 GeV proton gun, a run action (with CSV output), an event
 * action, a tracking action, and a stepping action on each worker thread.
 */
class G4OCCTActionInitialization : public G4VUserActionInitialization {
public:
  G4OCCTActionInitialization()           = default;
  ~G4OCCTActionInitialization() override = default;

  void BuildForMaster() const override;
  void Build() const override;

private:
  mutable G4OCCTOutputConfig fConfig;
};

#endif // G4OCCT_APP_G4OCCTActionInitialization_hh
