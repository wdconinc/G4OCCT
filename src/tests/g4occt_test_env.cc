// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "G4OCCTTestExceptionHandler.hh"

#include <G4StateManager.hh>
#include <gtest/gtest.h>

/**
 * @brief Google Test global environment that installs G4OCCTTestExceptionHandler.
 *
 * Registered via a global static so it takes effect for all GTest binaries
 * that link G4OCCTTestSupport, without requiring a custom main().
 */
class G4OCCTTestEnv : public ::testing::Environment {
public:
  void SetUp() override {
    fPreviousHandler = G4StateManager::GetStateManager()->GetExceptionHandler();
    G4StateManager::GetStateManager()->SetExceptionHandler(
        new G4OCCTTestExceptionHandler(fPreviousHandler));
  }

  void TearDown() override {
    auto* ourHandler = G4StateManager::GetStateManager()->GetExceptionHandler();
    G4StateManager::GetStateManager()->SetExceptionHandler(fPreviousHandler);
    delete ourHandler;
  }

private:
  G4VExceptionHandler* fPreviousHandler = nullptr;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static ::testing::Environment* const g_g4occt_test_env =
    ::testing::AddGlobalTestEnvironment(new G4OCCTTestEnv);
