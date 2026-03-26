// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file main.cc
/// @brief Entry point for the g4occt interactive Geant4 session.
///
/// Usage:
/// @code
/// g4occt [-m macro] [-u UIsession] [-t nThreads] [file.step ...] [file.xml ...]
/// @endcode
///
/// Positional arguments are classified by file extension:
///  - @c .step / @c .stp   — STEP geometry file (solid or assembly, auto-detected)
///  - @c .xml               — G4OCCT material-map XML file

#include "G4OCCTActionInitialization.hh"
#include "G4OCCTAppConfig.hh"
#include "G4OCCTDetectorConstruction.hh"

#include <FTFP_BERT.hh>
#include <G4RunManagerFactory.hh>
#include <G4UIExecutive.hh>
#include <G4UIcommand.hh>
#include <G4UImanager.hh>
#include <G4VisExecutive.hh>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

void PrintUsage(const char* prog) {
  G4cerr << "Usage: " << prog
         << " [-m macro] [-u UIsession] [-t nThreads]"
            " [file.step|file.stp ...] [file.xml ...]"
         << G4endl;
  G4cerr << "  -m macro      Execute Geant4 macro file (batch mode)." << G4endl;
  G4cerr << "  -u UIsession  Select UI session (Qt, Xm, tcsh, …)." << G4endl;
  G4cerr << "  -t nThreads   Number of worker threads (multi-threaded builds only)." << G4endl;
  G4cerr << "  file.step     Load STEP file (solid or assembly, auto-detected)." << G4endl;
  G4cerr << "  file.xml      Load G4OCCT material-map XML file." << G4endl;
}

/// Return the lower-case file extension (including the dot), e.g. ".step".
std::string FileExtension(const std::string& path) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

} // namespace

int main(int argc, char** argv) {
  G4String macro;
  G4String session;
#ifdef G4MULTITHREADED
  G4int nThreads = 0;
#endif
  std::vector<std::string> stepFiles;
  std::vector<std::string> materialFiles;

  // ── Parse arguments ────────────────────────────────────────────────────────
  for (G4int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);

    if (arg == "-m") {
      if (++i >= argc) {
        PrintUsage(argv[0]);
        return 1;
      }
      macro = argv[i];
    } else if (arg == "-u") {
      if (++i >= argc) {
        PrintUsage(argv[0]);
        return 1;
      }
      session = argv[i];
    } else if (arg == "-t") {
#ifdef G4MULTITHREADED
      if (++i >= argc) {
        PrintUsage(argv[0]);
        return 1;
      }
      nThreads = G4UIcommand::ConvertToInt(argv[i]);
#else
      G4cerr << "Warning: -t option ignored (Geant4 built without multi-threading)." << G4endl;
      ++i; // skip the value
#endif
    } else if (arg[0] == '-') {
      G4cerr << "Unknown option: " << arg << G4endl;
      PrintUsage(argv[0]);
      return 1;
    } else {
      // Positional argument — classify by extension
      const auto ext = FileExtension(arg);
      if (ext == ".step" || ext == ".stp") {
        stepFiles.push_back(arg);
      } else if (ext == ".xml") {
        materialFiles.push_back(arg);
      } else {
        G4cerr << "Unrecognised file type (expected .step, .stp, or .xml): " << arg << G4endl;
        PrintUsage(argv[0]);
        return 1;
      }
    }
  }

  // ── Interactive mode: create UI executive before run manager ──────────────
  G4UIExecutive* ui = nullptr;
  if (macro.empty()) {
    ui = new G4UIExecutive(argc, argv, session);
  }

  // ── Run manager ───────────────────────────────────────────────────────────
  auto* runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);
#ifdef G4MULTITHREADED
  if (nThreads > 0) {
    runManager->SetNumberOfThreads(nThreads);
  }
#endif

  // ── Detector construction ─────────────────────────────────────────────────
  auto* detConstruction = new G4OCCTDetectorConstruction;

  // Pre-load material maps so they are available when Construct() is called.
  for (const auto& f : materialFiles) {
    detConstruction->LoadMaterial(f);
  }
  // Queue STEP files for loading during Construct().
  for (const auto& f : stepFiles) {
    detConstruction->AddSTEP(f);
  }

  runManager->SetUserInitialization(detConstruction);
  runManager->SetUserInitialization(new FTFP_BERT(/*verbosity=*/0));
  runManager->SetUserInitialization(new G4OCCTActionInitialization);

  // ── Visualisation ─────────────────────────────────────────────────────────
  auto* visManager = new G4VisExecutive("Quiet");
  visManager->Initialize();

  // ── Macro search path ─────────────────────────────────────────────────────
  // Register the installed data directory first, then the build-tree copy.
  // This satisfies both `make install` users and in-tree development/testing.
  auto* UImanager = G4UImanager::GetUIpointer();
  UImanager->SetMacroSearchPath(G4String(G4OCCT_MACRO_DIR_INSTALL) + ":" +
                                G4OCCT_MACRO_DIR_BUILD);

  // ── Execute macro or start interactive session ────────────────────────────
  int exitCode = 0;

  if (!macro.empty()) {
    exitCode = UImanager->ApplyCommand("/control/execute " + macro);
  } else {
    UImanager->ApplyCommand("/control/execute init_vis.mac");
    if (ui->IsGUI()) {
      UImanager->ApplyCommand("/control/execute gui.mac");
    }
    ui->SessionStart();
    delete ui;
  }

  delete visManager;
  delete runManager;
  return exitCode != 0 ? 1 : 0;
}
