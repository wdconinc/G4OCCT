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
#include "G4OCCTDetectorConstruction.hh"

#include <FTFP_BERT.hh>
#include <G4RunManagerFactory.hh>
#include <G4UIExecutive.hh>
#include <G4UIcommand.hh>
#include <G4UImanager.hh>
#include <G4VisExecutive.hh>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

/// Return the directory that contains the running executable, or an empty path
/// on failure.  Uses @c /proc/self/exe on Linux and @c _NSGetExecutablePath on
/// macOS so that the result survives symlinks and spack relocation.
std::filesystem::path ExeDir() {
#if defined(__linux__)
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec)
    return p.parent_path();
#elif defined(__APPLE__)
  char buf[PATH_MAX];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0)
    return std::filesystem::canonical(buf).parent_path();
#endif
  return {};
}

/// Build the colon-separated macro search path using a three-tier fallback:
///  1. @c G4OCCT_MACRO_PATH environment variable (user/admin override).
///  2. Runtime-detected path relative to the executable
///     (@c <prefix>/share/g4occt/macros) — correct after spack relocation.
///  3. Compile-time build-tree path @c G4OCCT_MACRO_DIR_BUILD — for in-tree
///     development and CTest runs.
///
/// All existing, non-empty entries are appended in order so that Geant4's
/// macro search tries them in priority sequence.
G4String BuildMacroSearchPath() {
  std::vector<std::string> dirs;

  // 1. Environment variable override.
  if (const char* env = std::getenv("G4OCCT_MACRO_PATH"))
    if (*env != '\0')
      dirs.emplace_back(env);

  // 2. Runtime path: exe/../share/g4occt/macros.
  auto exeDir = ExeDir();
  if (!exeDir.empty()) {
    auto runtimeDir = exeDir.parent_path() / "share" / "g4occt" / "macros";
    dirs.push_back(runtimeDir.string());
  }

  // 3. Build-tree compile-time constant (developer / CTest fallback).
  dirs.emplace_back(G4OCCT_MACRO_DIR_BUILD);

  G4String path;
  for (const auto& d : dirs) {
    if (!path.empty())
      path += ":";
    path += d;
  }
  return path;
}

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
  if (dot == std::string::npos)
    return {};
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
      // Require an explicit value for -t in all builds, and ensure we don't
      // accidentally treat the next option token (e.g. "-m") as that value.
      if (i + 1 >= argc) {
        PrintUsage(argv[0]);
        return 1;
      }
      const std::string nextArg(argv[i + 1]);
      if (nextArg[0] == '-') {
        PrintUsage(argv[0]);
        return 1;
      }
      ++i; // move to the value for -t
#ifdef G4MULTITHREADED
      nThreads = G4UIcommand::ConvertToInt(argv[i]);
#else
      G4cerr << "Warning: -t option (" << argv[i]
             << ") ignored (Geant4 built without multi-threading)." << G4endl;
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
  // Only initialize vis in interactive mode; batch runs don't require it and
  // may run in environments without display/vis support.
  G4VisExecutive* visManager = nullptr;
  if (ui != nullptr) {
    visManager = new G4VisExecutive("Quiet");
    visManager->Initialize();
  }

  // ── Macro search path ─────────────────────────────────────────────────────
  // Three-tier fallback so the binary works after spack relocation:
  //  1. G4OCCT_MACRO_PATH env var  — user/admin override
  //  2. <exe>/../share/g4occt/macros — runtime-relative (survives relocation)
  //  3. G4OCCT_MACRO_DIR_BUILD     — in-tree build / CTest
  auto* UImanager = G4UImanager::GetUIpointer();
  UImanager->SetMacroSearchPath(BuildMacroSearchPath());

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
