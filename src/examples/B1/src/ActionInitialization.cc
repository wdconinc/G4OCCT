// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file ActionInitialization.cc
/// @brief Implementation of the B1 action initialization.

#include "ActionInitialization.hh"

#include "EventAction.hh"
#include "PrimaryGeneratorAction.hh"
#include "RunAction.hh"
#include "SteppingAction.hh"

void ActionInitialization::BuildForMaster() const {
  auto* runAction = new RunAction();
  SetUserAction(runAction);
}

void ActionInitialization::Build() const {
  SetUserAction(new PrimaryGeneratorAction());

  auto* runAction   = new RunAction();
  auto* eventAction = new EventAction(runAction);

  SetUserAction(runAction);
  SetUserAction(eventAction);
  SetUserAction(new SteppingAction(eventAction));
}
