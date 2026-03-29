// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#pragma once

#include <G4ExceptionSeverity.hh>
#include <G4StateManager.hh>
#include <G4VExceptionHandler.hh>

#include <string>

/**
 * @brief G4VExceptionHandler that captures fatal G4Exceptions instead of aborting.
 *
 * When installed, any `FatalException` or `FatalErrorInArgument` G4Exception is
 * recorded (sets @c caught = true and stores the @c code) and `Notify()` returns
 * `false` so Geant4 does **not** call `abort()`.  This lets fatal-path source
 * lines execute in-process, where gcov can record them, complementing
 * `EXPECT_DEATH` tests that verify abort behaviour but run in a forked child.
 *
 * Non-fatal severities (e.g. `JustWarning`) are forwarded to @p previousHandler
 * so that the existing @c G4OCCTTestExceptionHandler warning-to-ADD_FAILURE
 * behaviour remains active while the guard is installed.
 *
 * ### Usage
 * ```cpp
 * G4OCCTFatalCatchGuard guard;
 * someFunction();                        // triggers FatalException
 * EXPECT_TRUE(guard.catcher.caught);
 * EXPECT_EQ(guard.catcher.code, "G4OCCT_XYZ");
 * // guard destructor restores the previous handler automatically
 * ```
 */
struct G4OCCTFatalCatcher : public G4VExceptionHandler {
  bool        caught = false;
  std::string code;
  /// Previously active handler; non-fatal severities are delegated here.
  G4VExceptionHandler* prev = nullptr;

  G4bool Notify(const char* originOfException, const char* exceptionCode,
                G4ExceptionSeverity severity, const char* description) override {
    if (severity == FatalException || severity == FatalErrorInArgument) {
      caught = true;
      code   = exceptionCode;
      return false; // suppress abort — execution continues past G4Exception()
    }
    // Forward all non-fatal severities to the previously-installed handler so
    // that JustWarning codes still reach G4OCCTTestExceptionHandler and produce
    // ADD_FAILURE() as expected.
    if (prev != nullptr) {
      return prev->Notify(originOfException, exceptionCode, severity, description);
    }
    return false; // non-fatal: don't abort if no previous handler
  }
};

/**
 * @brief RAII guard that installs G4OCCTFatalCatcher for the duration of a scope.
 *
 * Captures the current Geant4 exception handler on construction, installs
 * @c G4OCCTFatalCatcher (forwarding non-fatal calls to the saved handler), and
 * restores the original handler on destruction.
 */
struct G4OCCTFatalCatchGuard {
  G4OCCTFatalCatcher catcher;

  G4OCCTFatalCatchGuard() {
    catcher.prev = G4StateManager::GetStateManager()->GetExceptionHandler();
    G4StateManager::GetStateManager()->SetExceptionHandler(&catcher);
  }
  ~G4OCCTFatalCatchGuard() {
    G4StateManager::GetStateManager()->SetExceptionHandler(catcher.prev);
  }

  // Non-copyable, non-movable.
  G4OCCTFatalCatchGuard(const G4OCCTFatalCatchGuard&)            = delete;
  G4OCCTFatalCatchGuard& operator=(const G4OCCTFatalCatchGuard&) = delete;
};
