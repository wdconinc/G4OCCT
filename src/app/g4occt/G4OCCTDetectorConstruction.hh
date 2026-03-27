// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTDetectorConstruction.hh
/// @brief Dynamic detector construction for the g4occt interactive tool.

#ifndef G4OCCT_APP_G4OCCTDetectorConstruction_hh
#define G4OCCT_APP_G4OCCTDetectorConstruction_hh

#include <G4String.hh>
#include <G4ThreeVector.hh>
#include <G4VUserDetectorConstruction.hh>

#include <memory>
#include <string>
#include <vector>

class G4GenericMessenger;
class G4VPhysicalVolume;

/**
 * @brief Dynamically constructs a Geant4 world from STEP solids and assemblies.
 *
 * Geometry is accumulated via public methods (called from positional CLI
 * arguments in @c main()) or via the @c /G4OCCT/ messenger commands registered
 * in the constructor.
 *
 * ## Messenger commands
 *
 * | Command | Effect |
 * |---------|--------|
 * | `/G4OCCT/load/step <file>` | Auto-detect and queue as solid or assembly |
 * | `/G4OCCT/load/assembly <file>` | Force assembly import via XDE |
 * | `/G4OCCT/load/material <file>` | Parse material-map XML |
 * | `/G4OCCT/world/setMaterial <name>` | World volume material (default G4_AIR) |
 * | `/G4OCCT/world/setSize <x> <y> <z> <unit>` | World half-size as 3-vector |
 * | `/G4OCCT/vis/open [driver]` | Open a visualisation driver |
 * | `/G4OCCT/vis/scene` | Redraw and refresh the current scene |
 *
 * ## World sizing
 *
 * If no explicit size is set, `Construct()` computes the world half-size as
 * 110 % of the axis-aligned bounding box of all placed volumes.  An empty
 * world defaults to a 1 m³ box.
 *
 * ## Material resolution
 *
 * Resolution order: @c G4OCCTMaterialMap → `G4NistManager` lookup → fatal
 * `G4Exception`.  Passing material XML files via `LoadMaterial()` before
 * `Construct()` is strongly recommended for STEP files that embed non-NIST
 * material names.
 */
class G4OCCTDetectorConstruction : public G4VUserDetectorConstruction {
public:
  G4OCCTDetectorConstruction();
  ~G4OCCTDetectorConstruction() override;

  G4VPhysicalVolume* Construct() override;

  // ── Public API (callable from main() or messenger) ───────────────────────

  /// Queue a material-map XML file to be loaded at construction time.
  void LoadMaterial(G4String xmlFile);

  /// Queue a STEP file as a solid or assembly (auto-detected).
  void AddSTEP(G4String stepFile);

  /// Explicitly queue a STEP file as a G4OCCTSolid with an optional material override.
  void AddSolid(G4String stepFile, G4String materialName = "");

  /// Explicitly queue a STEP file as a G4OCCTAssemblyVolume.
  void AddAssembly(G4String stepFile);

  /// Set the world volume material by Geant4/NIST name.
  void SetWorldMaterial(G4String name) { fWorldMaterial = std::move(name); }

  /// Set the world half-size.  Overrides auto-sizing.
  void SetWorldHalfSize(G4ThreeVector halfSize) { fWorldHalfSize = halfSize; }

private:
  struct SolidEntry {
    std::string file;
    std::string material; ///< empty → auto-resolve
  };

  struct AssemblyEntry {
    std::string file;
  };

  // Material XML files are stored and loaded at Construct() time so that
  // all maps can be merged before any geometry is built.
  std::vector<std::string> fMaterialXmlFiles;
  std::vector<SolidEntry> fSolidEntries;
  std::vector<AssemblyEntry> fAssemblyEntries;
  G4String fWorldMaterial{"G4_AIR"};
  G4ThreeVector fWorldHalfSize{0, 0, 0}; ///< zero → auto-size

  std::unique_ptr<G4GenericMessenger> fLoadMessenger;
  std::unique_ptr<G4GenericMessenger> fWorldMessenger;
  std::unique_ptr<G4GenericMessenger> fVisMessenger;

  void DefineMessengers();

  /// Issue a UI command string via G4UImanager.
  static void UI(const G4String& cmd);

  /// Visualisation helpers bound to /G4OCCT/vis/ commands.
  void OpenViewer(G4String driver);
  void RefreshScene();
};

#endif // G4OCCT_APP_G4OCCTDetectorConstruction_hh
