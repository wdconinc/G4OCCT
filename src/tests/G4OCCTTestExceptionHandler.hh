// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#pragma once

#include <G4ExceptionSeverity.hh>
#include <G4VExceptionHandler.hh>
#include <gtest/gtest.h>
#include <string_view>

/**
 * @brief Google Test exception handler for G4OCCT unit tests.
 *
 * Installed as the Geant4 exception handler during test runs.  Any
 * JustWarning G4Exception whose code starts with @c G4OCCT_ or equals
 * @c GeomMgt1001 is converted into a Google Test failure via
 * @c ADD_FAILURE(), so unexpected warnings from our own code cause tests
 * to fail without aborting the process.
 *
 * G4Exception calls from Geant4-internal code (e.g. @c mat031 from
 * G4Material::FillVectors) are silently passed through.
 *
 * For non-JustWarning severities (FatalException, FatalErrorInArgument,
 * etc.) the call is forwarded to @p previousHandler if non-null, or the
 * function returns @c true so that Geant4 aborts the process—preserving
 * the expected fatal-abort semantics during tests.
 */
class G4OCCTTestExceptionHandler : public G4VExceptionHandler {
public:
  explicit G4OCCTTestExceptionHandler(G4VExceptionHandler* previousHandler)
      : fPreviousHandler(previousHandler) {}

  G4bool Notify(const char* originOfException, const char* exceptionCode,
                G4ExceptionSeverity severity, const char* description) override {
    if (severity == JustWarning) {
      std::string_view code(exceptionCode);
      if (code.starts_with("G4OCCT_") || code == "GeomMgt1001") {
        ADD_FAILURE() << "Unexpected G4Exception [" << exceptionCode << "] from "
                      << originOfException << ": " << description;
      }
      return false;
    }
    // For fatal or other severities, delegate to the previous handler so that
    // abort semantics are preserved.  If there is no previous handler, return
    // true to tell Geant4 to abort the process.
    if (fPreviousHandler != nullptr) {
      return fPreviousHandler->Notify(originOfException, exceptionCode, severity, description);
    }
    return severity == FatalException || severity == FatalErrorInArgument;
  }

private:
  G4VExceptionHandler* fPreviousHandler = nullptr;
};
