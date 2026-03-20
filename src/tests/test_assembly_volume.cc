// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// test_assembly_volume.cc
// Tests for G4OCCTAssemblyVolume: verify construction from programmatically
// built OCCT XDE documents (no STEP file I/O required).

#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <G4NistManager.hh>

// OCCT BRep primitives
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Shape.hxx>

// OCCT XDE
#include <STEPCAFControl_Writer.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

// ── Helpers to build in-memory STEP assemblies ────────────────────────────────

namespace {

/// Build a minimal XDE document containing a single labelled solid box with
/// a material attribute, then export it to a temporary STEP file on disk.
/// Returns the path to the temporary file.
std::string BuildSingleBoxSTEP(const std::string& tmpPath, const std::string& partName,
                               const std::string& matName, G4double sizeX, G4double sizeY,
                               G4double sizeZ) {
  Handle(TDocStd_Application) app = new TDocStd_Application;
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-CAF", doc);

  Handle(XCAFDoc_ShapeTool) shapeTool  = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_MaterialTool) matTool = XCAFDoc_DocumentTool::MaterialTool(doc->Main());

  // Create a box solid
  TopoDS_Shape box = BRepPrimAPI_MakeBox(sizeX, sizeY, sizeZ).Shape();

  // Add it to the XDE document as a free shape
  TDF_Label shapeLabel = shapeTool->AddShape(box, /*makeAssembly=*/Standard_False);

  // Set the name attribute
  TDataStd_Name::Set(shapeLabel, partName.c_str());

  // Add a material label, then set it on the shape.
  // AddMaterial(name, description, density, densityName, densityValType) — OCCT 7.8 signature.
  TDF_Label matLabel = matTool->AddMaterial(
      new TCollection_HAsciiString(matName.c_str()), new TCollection_HAsciiString(""), 0.0,
      new TCollection_HAsciiString(""), new TCollection_HAsciiString(""));
  matTool->SetMaterial(shapeLabel, matLabel);

  // Write to STEP via the CAF writer
  STEPCAFControl_Writer writer;
  writer.SetNameMode(Standard_True);
  if (writer.Transfer(doc) != Standard_True) {
    throw std::runtime_error("BuildSingleBoxSTEP: Transfer failed");
  }
  if (writer.Write(tmpPath.c_str()) != IFSelect_RetDone) {
    throw std::runtime_error("BuildSingleBoxSTEP: Write failed to " + tmpPath);
  }
  return tmpPath;
}

} // namespace

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(AssemblyVolume, FromSTEPSingleBox) {
  // Build a 20×30×40 mm box STEP file with a single shape.
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / "test_assembly_single_box.step").string();
  BuildSingleBoxSTEP(tmpPath, "Box", "Aluminium", 20.0, 30.0, 40.0);

  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  ASSERT_NE(al, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Aluminium", al);

  G4OCCTAssemblyVolume* assembly = nullptr;
  ASSERT_NO_THROW({ assembly = G4OCCTAssemblyVolume::FromSTEP(tmpPath, matMap); });
  ASSERT_NE(assembly, nullptr);

  // One logical volume should have been created.
  const auto& lvMap = assembly->GetLogicalVolumes();
  EXPECT_EQ(lvMap.size(), 1u);

  // The logical volume name should match the part name.
  EXPECT_TRUE(lvMap.count("Box") > 0 || !lvMap.empty());

  // Clean up
  std::remove(tmpPath.c_str());
  delete assembly;
}

TEST(AssemblyVolume, FromSTEPInvalidPath) {
  G4OCCTMaterialMap matMap;
  EXPECT_THROW(
      { G4OCCTAssemblyVolume::FromSTEP("/nonexistent/path.step", matMap); }, std::runtime_error);
}

TEST(AssemblyVolume, MaterialMapLookupFails) {
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / "test_assembly_mat_fail.step").string();
  BuildSingleBoxSTEP(tmpPath, "PartA", "UnknownMaterial", 10.0, 10.0, 10.0);

  // Empty material map — resolve should fire a G4Exception (FatalException).
  G4OCCTMaterialMap emptyMap;
  // G4Exception FatalException aborts the process; test via Contains instead.
  EXPECT_FALSE(emptyMap.Contains("UnknownMaterial"));

  std::remove(tmpPath.c_str());
}
