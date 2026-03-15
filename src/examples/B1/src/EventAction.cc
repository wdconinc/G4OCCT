// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file EventAction.cc
/// @brief Implementation of the B1 event action.

#include "EventAction.hh"

#include "RunAction.hh"

#include <G4Event.hh>
#include <G4RunManager.hh>
#include <G4UnitsTable.hh>
#include <G4ios.hh>

EventAction::EventAction(RunAction* runAction) : fRunAction(runAction) {}

void EventAction::BeginOfEventAction(const G4Event* /* event */) {
  fEdep = 0.0;
}

void EventAction::EndOfEventAction(const G4Event* /* event */) {
  fRunAction->AddEdep(fEdep);
}
