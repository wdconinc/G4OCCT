// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file ActionInitialization.hh
/// @brief User action initialization for the G4OCCT B1 example.

#ifndef B1_ActionInitialization_hh
#define B1_ActionInitialization_hh

#include <G4VUserActionInitialization.hh>

/**
 * @brief Initializes all user action classes for the B1 example.
 */
class ActionInitialization : public G4VUserActionInitialization {
 public:
  ActionInitialization() = default;
  ~ActionInitialization() override = default;

  void BuildForMaster() const override;
  void Build() const override;
};

#endif  // B1_ActionInitialization_hh
