// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTDetectorConstruction.cc
/// @brief Dynamic detector construction for the g4occt interactive tool.

#include "G4OCCTDetectorConstruction.hh"

#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"
#include "G4OCCT/G4OCCTMaterialMapReader.hh"
#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <G4Box.hh>
#include <G4Exception.hh>
#include <G4GenericMessenger.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4SystemOfUnits.hh>
#include <G4UImanager.hh>

#include <filesystem>
#include <memory>
#include <string>

// ── Helper: detect whether a STEP file is an assembly ────────────────────────

namespace {

/// Returns true if the STEP file contains more than one top-level free shape
/// or if its single top-level shape is an XDE assembly node.
bool IsAssemblySTEP(const std::string& path) {
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-XCAF", doc);

  STEPCAFControl_Reader reader;
  reader.SetColorMode(false);
  reader.SetNameMode(true);
  reader.SetMatMode(true);
  if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
    return false; // let FromSTEP raise the proper exception later
  }
  if (!reader.Transfer(doc)) {
    return false;
  }

  Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  TDF_LabelSequence freeShapes;
  shapeTool->GetFreeShapes(freeShapes);

  if (freeShapes.Length() > 1) {
    return true;
  }
  if (freeShapes.Length() == 1 && shapeTool->IsAssembly(freeShapes.Value(1))) {
    return true;
  }
  return false;
}

} // namespace

// ── G4OCCTDetectorConstruction ────────────────────────────────────────────────

G4OCCTDetectorConstruction::G4OCCTDetectorConstruction() {
  DefineMessengers();
}

G4OCCTDetectorConstruction::~G4OCCTDetectorConstruction() = default;

void G4OCCTDetectorConstruction::DefineMessengers() {
  fLoadMessenger =
      std::make_unique<G4GenericMessenger>(this, "/G4OCCT/load/", "Load geometry and materials");

  fLoadMessenger->DeclareMethod("step", &G4OCCTDetectorConstruction::AddSTEP)
      .SetGuidance("Load a STEP file — auto-detects solid vs assembly.")
      .SetParameterName("filename", false);

  fLoadMessenger->DeclareMethod("assembly", &G4OCCTDetectorConstruction::AddAssembly)
      .SetGuidance("Load a STEP file as an XDE assembly.")
      .SetParameterName("filename", false);

  fLoadMessenger->DeclareMethod("material", &G4OCCTDetectorConstruction::LoadMaterial)
      .SetGuidance("Load a G4OCCT material-map XML file.")
      .SetParameterName("filename", false);

  fWorldMessenger =
      std::make_unique<G4GenericMessenger>(this, "/G4OCCT/world/", "Configure the world volume");

  fWorldMessenger->DeclareProperty("setMaterial", fWorldMaterial)
      .SetGuidance("World volume material name (default: G4_AIR).")
      .SetParameterName("name", false);

  fWorldMessenger
      ->DeclareMethodWithUnit("setSize", "mm", &G4OCCTDetectorConstruction::SetWorldHalfSize)
      .SetGuidance("World half-size as a 3-vector.")
      .SetParameterName("halfSize", false);

  fVisMessenger =
      std::make_unique<G4GenericMessenger>(this, "/G4OCCT/vis/", "Visualisation helpers");

  fVisMessenger->DeclareMethod("open", &G4OCCTDetectorConstruction::OpenViewer)
      .SetGuidance("Open a visualisation driver (default: OGL).")
      .SetParameterName("driver", true)
      .SetDefaultValue("OGL");

  fVisMessenger->DeclareMethod("scene", &G4OCCTDetectorConstruction::RefreshScene)
      .SetGuidance("Redraw and refresh the current visualisation scene.");
}

void G4OCCTDetectorConstruction::UI(const G4String& cmd) {
  G4UImanager::GetUIpointer()->ApplyCommand(cmd);
}

void G4OCCTDetectorConstruction::LoadMaterial(G4String xmlFile) {
  fMaterialXmlFiles.push_back(std::string(xmlFile));
}

void G4OCCTDetectorConstruction::AddSTEP(G4String stepFile) {
  if (IsAssemblySTEP(std::string(stepFile))) {
    AddAssembly(std::move(stepFile));
  } else {
    AddSolid(std::move(stepFile), "");
  }
}

void G4OCCTDetectorConstruction::AddSolid(G4String stepFile, G4String materialName) {
  fSolidEntries.push_back({std::string(stepFile), std::string(materialName)});
}

void G4OCCTDetectorConstruction::AddAssembly(G4String stepFile) {
  fAssemblyEntries.push_back({std::string(stepFile)});
}

void G4OCCTDetectorConstruction::OpenViewer(G4String driver) {
  if (driver.empty()) {
    driver = "OGL";
  }
  UI("/vis/open " + driver);
}

void G4OCCTDetectorConstruction::RefreshScene() {
  UI("/vis/drawVolume");
  UI("/vis/viewer/refresh");
}

G4VPhysicalVolume* G4OCCTDetectorConstruction::Construct() {
  // ── Build combined material map from all queued XML files ─────────────────
  G4OCCTMaterialMap materialMap;
  G4OCCTMaterialMapReader reader;
  for (const auto& xmlFile : fMaterialXmlFiles) {
    G4OCCTMaterialMap loaded = reader.ReadFile(xmlFile);
    // Merge into the combined map; later files override earlier entries for
    // the same STEP material name.
    materialMap.Merge(loaded);
  }
  // If no XML files were loaded, materialMap remains empty; NIST fallback
  // applies per-solid below.

  // ── Collect bounding box info for world auto-sizing ───────────────────────
  Bnd_Box totalBounds;
  bool hasBounds = false;

  // ── Build STEP solids ──────────────────────────────────────────────────────
  std::vector<G4LogicalVolume*> solidLogicals;
  for (std::size_t i = 0; i < fSolidEntries.size(); ++i) {
    const auto& entry = fSolidEntries[i];
    auto name = std::filesystem::path(entry.file).stem().string();
    if (fSolidEntries.size() > 1) {
      name += "_" + std::to_string(i);
    }

    auto* solid = G4OCCTSolid::FromSTEP(name, entry.file);

    Bnd_Box bbox;
    BRepBndLib::AddOptimal(solid->GetOCCTShape(), bbox, /*useTriangulation=*/Standard_False);
    totalBounds.Add(bbox);
    hasBounds = true;

    // Resolve material
    G4Material* mat = nullptr;
    if (!entry.material.empty()) {
      if (materialMap.Contains(entry.material)) {
        mat = materialMap.Resolve(entry.material);
      } else {
        mat = G4NistManager::Instance()->FindOrBuildMaterial(entry.material);
        if (!mat) {
          G4Exception("G4OCCTDetectorConstruction::Construct", "G4OCCT_App_MatNotFound",
                      FatalException,
                      ("Cannot resolve material '" + entry.material + "' for solid '" + name + "'")
                          .c_str());
        }
      }
    } else {
      mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
    }

    auto* lv = new G4LogicalVolume(solid, mat, name + "_lv");
    solidLogicals.push_back(lv);
  }

  // ── Build STEP assemblies ──────────────────────────────────────────────────
  std::vector<std::unique_ptr<G4OCCTAssemblyVolume>> assemblies;
  for (const auto& entry : fAssemblyEntries) {
    auto assembly = std::unique_ptr<G4OCCTAssemblyVolume>(
        G4OCCTAssemblyVolume::FromSTEP(entry.file, materialMap));
    for (const auto& [lvName, lv] : assembly->GetLogicalVolumes()) {
      if (auto* occSolid = dynamic_cast<G4OCCTSolid*>(lv->GetSolid())) {
        Bnd_Box bbox;
        BRepBndLib::AddOptimal(occSolid->GetOCCTShape(), bbox, Standard_False);
        totalBounds.Add(bbox);
        hasBounds = true;
      }
    }
    assemblies.push_back(std::move(assembly));
  }

  // ── Compute world half-size ────────────────────────────────────────────────
  G4ThreeVector halfSize = fWorldHalfSize;
  if (halfSize.mag2() == 0.0) {
    if (hasBounds && !totalBounds.IsVoid()) {
      Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
      totalBounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
      const double pad = 1.1;
      halfSize = G4ThreeVector(pad * std::max(std::abs(xMax), std::abs(xMin)) * mm,
                               pad * std::max(std::abs(yMax), std::abs(yMin)) * mm,
                               pad * std::max(std::abs(zMax), std::abs(zMin)) * mm);
      // Enforce a minimum 10 cm half-size on each axis
      for (int k = 0; k < 3; ++k) {
        if (halfSize[k] < 100.0 * mm) halfSize[k] = 100.0 * mm;
      }
    } else {
      halfSize = G4ThreeVector(500 * mm, 500 * mm, 500 * mm); // 1 m³ default
    }
  }

  // ── World volume ───────────────────────────────────────────────────────────
  auto* worldMat = G4NistManager::Instance()->FindOrBuildMaterial(fWorldMaterial);
  if (!worldMat) {
    G4Exception("G4OCCTDetectorConstruction::Construct", "G4OCCT_App_WorldMat", FatalException,
                ("Cannot find world material: " + std::string(fWorldMaterial)).c_str());
  }

  auto* worldSolid = new G4Box("world", halfSize.x(), halfSize.y(), halfSize.z());
  auto* worldLV    = new G4LogicalVolume(worldSolid, worldMat, "world_lv");
  auto* worldPV    = new G4PVPlacement(nullptr, G4ThreeVector(), worldLV, "world_pv",
                                       nullptr, false, 0);

  // ── Place solids at origin ─────────────────────────────────────────────────
  for (std::size_t i = 0; i < solidLogicals.size(); ++i) {
    auto* lv = solidLogicals[i];
    new G4PVPlacement(nullptr, G4ThreeVector(), lv, lv->GetName() + "_pv", worldLV, false,
                      static_cast<G4int>(i));
  }

  // ── Imprint assemblies at origin ───────────────────────────────────────────
  for (auto& assembly : assemblies) {
    G4Transform3D identity;
    assembly->MakeImprint(worldLV, identity);
  }

  return worldPV;
}
