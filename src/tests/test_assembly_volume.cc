// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

// test_assembly_volume.cc
// Tests for G4OCCTAssemblyVolume: verify construction from programmatically
// built OCCT XDE documents exported to temporary STEP files on disk.

#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <G4Box.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4RotationMatrix.hh>
#include <G4ThreeVector.hh>
#include <G4VPhysicalVolume.hh>

// OCCT BRep primitives
#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

// OCCT STEP / XDE
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

// ── Helpers to build in-memory STEP assemblies ────────────────────────────────

namespace {

G4Material* GetTestMaterial(const std::string& name)
{
  return G4NistManager::Instance()->FindOrBuildMaterial(name);
}

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

/// Build an XDE document with a top-level assembly containing two instances of
/// the same 10×10×10 mm box.  The first instance has an identity placement;
/// the second is translated by @p txX mm along X.  Exporting and reimporting
/// this document exercises the LocationToTrsf code path.
std::string BuildTwoBoxAssemblySTEP(const std::string& tmpPath, double txX) {
  Handle(TDocStd_Application) app = new TDocStd_Application;
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-CAF", doc);

  Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());

  // Box prototype: both instances share the same TShape geometry.
  TopoDS_Shape protoBox = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();

  // Build a compound that contains two instances of the same box at different
  // positions.  Using the same TShape pointer for both sub-shapes enables
  // XDE's shape-sharing detection: AddShape(compound, makeAssembly=true)
  // creates one prototype label and two reference labels, the second of which
  // carries an XCAFDoc_Location with the requested translation.
  BRep_Builder brepBuilder;
  TopoDS_Compound compound;
  brepBuilder.MakeCompound(compound);
  brepBuilder.Add(compound, protoBox); // identity placement

  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(txX, 0.0, 0.0));
  brepBuilder.Add(compound, protoBox.Located(TopLoc_Location(trsf))); // translated

  // Create an XDE assembly from the compound.  makeAssembly=true causes OCCT
  // to decompose the compound into reference labels, one per sub-shape, each
  // carrying its sub-shape's location as an XCAFDoc_Location attribute.
  TDF_Label asmLabel = shapeTool->AddShape(compound, /*makeAssembly=*/Standard_True);
  TDataStd_Name::Set(asmLabel, "TwoBoxAssembly");

  // Assign names to the referred prototype label(s) so that the material-map
  // lookup in ImportLabel can resolve them.
  TDF_LabelSequence components;
  shapeTool->GetComponents(asmLabel, components, /*recursive=*/Standard_False);
  for (Standard_Integer i = 1; i <= components.Length(); ++i) {
    TDF_Label referred;
    if (shapeTool->GetReferredShape(components.Value(i), referred)) {
      TDataStd_Name::Set(referred, "BoxPart");
    }
  }

  STEPCAFControl_Writer writer;
  writer.SetNameMode(Standard_True);
  if (writer.Transfer(doc) != Standard_True)
    throw std::runtime_error("BuildTwoBoxAssemblySTEP: Transfer failed");
  if (writer.Write(tmpPath.c_str()) != IFSelect_RetDone)
    throw std::runtime_error("BuildTwoBoxAssemblySTEP: Write failed to " + tmpPath);
  return tmpPath;
}

} // namespace

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(AssemblyVolume, FromSTEPSingleBox) {
  // Build a 20×30×40 mm box STEP file with a single shape.
  const std::string uniqueSuffix = std::to_string(std::random_device{}());
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / ("test_assembly_single_box_" + uniqueSuffix + ".step")).string();
  BuildSingleBoxSTEP(tmpPath, "Box", "Aluminium", 20.0, 30.0, 40.0);

  G4Material* al = GetTestMaterial("G4_Al");
  ASSERT_NE(al, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Box", al);

  std::unique_ptr<G4OCCTAssemblyVolume> assembly;
  ASSERT_NO_THROW({ assembly.reset(G4OCCTAssemblyVolume::FromSTEP(tmpPath, matMap)); });
  ASSERT_NE(assembly, nullptr);

  // One logical volume should have been created.
  const auto& lvMap = assembly->GetLogicalVolumes();
  EXPECT_EQ(lvMap.size(), 1u);

  // The logical volume name should match the part name.
  EXPECT_EQ(lvMap.count("Box"), 1u);

  // Clean up
  std::filesystem::remove(tmpPath);
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

  // Empty material map — resolving the material during FromSTEP should fire
  // a G4Exception with FatalException severity and abort the process.
  G4OCCTMaterialMap emptyMap;
  EXPECT_DEATH(
      {
        G4OCCTAssemblyVolume* assembly = nullptr;
        assembly = G4OCCTAssemblyVolume::FromSTEP(tmpPath, emptyMap);
        // In case the implementation changes and no death occurs, avoid leaks.
        delete assembly;
      },
      "");

  std::remove(tmpPath.c_str());
}

TEST(AssemblyVolume, MaterialAssignedToLogicalVolume) {
  // Verify that the G4Material assigned in the material map is recorded on
  // the logical volume created by FromSTEP.
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / "test_assembly_mat_lv.step").string();
  BuildSingleBoxSTEP(tmpPath, "CopperPart", "Copper material", 10.0, 10.0, 10.0);

  G4Material* cu = G4NistManager::Instance()->FindOrBuildMaterial("G4_Cu");
  ASSERT_NE(cu, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("CopperPart", cu);

  G4OCCTAssemblyVolume* assembly = nullptr;
  ASSERT_NO_THROW({ assembly = G4OCCTAssemblyVolume::FromSTEP(tmpPath, matMap); });
  ASSERT_NE(assembly, nullptr);

  const auto& lvMap = assembly->GetLogicalVolumes();
  ASSERT_EQ(lvMap.count("CopperPart"), 1u);
  EXPECT_EQ(lvMap.at("CopperPart")->GetMaterial(), cu);

  std::remove(tmpPath.c_str());
  delete assembly;
}

TEST(AssemblyVolume, MaterialPreservedAfterMakeImprint) {
  // After MakeImprint the daughter physical volumes in the world logical
  // volume must carry the material that was specified in the material map.
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / "test_assembly_mat_imprint.step").string();
  BuildSingleBoxSTEP(tmpPath, "LeadPart", "Lead material", 10.0, 10.0, 10.0);

  G4Material* pb = G4NistManager::Instance()->FindOrBuildMaterial("G4_Pb");
  ASSERT_NE(pb, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("LeadPart", pb);

  G4OCCTAssemblyVolume* assembly = nullptr;
  ASSERT_NO_THROW({ assembly = G4OCCTAssemblyVolume::FromSTEP(tmpPath, matMap); });
  ASSERT_NE(assembly, nullptr);

  // Imprint the assembly into a temporary world volume.
  auto* worldBox  = new G4Box("MatImprintTestWorld", 100.0, 100.0, 100.0);
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  auto* worldLV   = new G4LogicalVolume(worldBox, air, "MatImprintTestWorldLV");
  G4ThreeVector pos;
  G4RotationMatrix rot;
  assembly->MakeImprint(worldLV, pos, &rot);

  // The single daughter should use the material from the map.
  ASSERT_EQ(worldLV->GetNoDaughters(), 1);
  const G4VPhysicalVolume* daughter = worldLV->GetDaughter(0);
  EXPECT_EQ(daughter->GetLogicalVolume()->GetMaterial(), pb);

  std::remove(tmpPath.c_str());
  delete assembly;
}

// ── New tests: multi-shape assembly and LocationToTrsf coverage ───────────────

TEST(AssemblyVolume, FromSTEPTripleBox) {
  // Load the triple-box-v1 fixture which contains three free-shape boxes all
  // named "Component".  Verifies that FromSTEP handles multi-shape STEP files
  // and that three logical volumes are created with deduplicated names.
  const std::filesystem::path fixtureDir =
      std::filesystem::path(__FILE__).parent_path() / "fixtures";
  const std::string stepPath =
      (fixtureDir / "assembly-comparison/triple-box-v1/shape.step").string();

  ASSERT_TRUE(std::filesystem::exists(stepPath)) << "Fixture not found: " << stepPath;

  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  ASSERT_NE(al, nullptr);

  // All three parts share the name "Component"; the material map key matches.
  G4OCCTMaterialMap matMap;
  matMap.Add("Component", al);

  G4OCCTAssemblyVolume* assembly = nullptr;
  ASSERT_NO_THROW({ assembly = G4OCCTAssemblyVolume::FromSTEP(stepPath, matMap); });
  ASSERT_NE(assembly, nullptr);

  // Three separate shape labels => three logical volumes with deduplicated names
  // ("Component", "Component_1", "Component_2").
  EXPECT_EQ(assembly->GetLogicalVolumes().size(), 3u);
  EXPECT_EQ(assembly->GetLogicalVolumes().count("Component"), 1u);
  EXPECT_EQ(assembly->GetLogicalVolumes().count("Component_1"), 1u);
  EXPECT_EQ(assembly->GetLogicalVolumes().count("Component_2"), 1u);

  // Further validate that the fixture behaves as a triple-box assembly by
  // imprinting it into an empty world volume and checking the resulting
  // daughter volumes. This helps detect silent corruption or modification
  // of the fixture geometry or labels.
  auto* worldBox = new G4Box("TripleBoxWorld", 500.0, 500.0, 500.0);
  // Reuse air material for the world; we only care about daughters' material.
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  ASSERT_NE(air, nullptr);
  auto* worldLV = new G4LogicalVolume(worldBox, air, "TripleBoxWorldLV");
  G4ThreeVector worldPos;
  G4RotationMatrix worldRot;
  assembly->MakeImprint(worldLV, worldPos, &worldRot);

  // The triple-box fixture is expected to produce exactly three daughter
  // physical volumes when imprinted into an empty world.
  ASSERT_EQ(worldLV->GetNoDaughters(), 3);
  for (G4int i = 0; i < worldLV->GetNoDaughters(); ++i) {
    const G4VPhysicalVolume* daughter = worldLV->GetDaughter(i);
    ASSERT_NE(daughter, nullptr);
    EXPECT_EQ(daughter->GetLogicalVolume()->GetMaterial(), al);
  }

  delete assembly;
}

TEST(AssemblyVolume, AssemblyWithTranslation) {
  // Build an XDE assembly whose second component is translated by 50 mm along X.
  // When the STEP is reimported, ImportLabel extracts the XCAFDoc_Location from
  // the reference label and calls LocationToTrsf, exercising that code path.
  // After MakeImprint the two daughter physical volumes must differ in X by
  // exactly kTranslationX.
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / "test_assembly_translation.step").string();
  constexpr double kTranslationX = 50.0; // mm
  BuildTwoBoxAssemblySTEP(tmpPath, kTranslationX);

  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  ASSERT_NE(al, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("BoxPart", al);

  G4OCCTAssemblyVolume* assembly = nullptr;
  ASSERT_NO_THROW({ assembly = G4OCCTAssemblyVolume::FromSTEP(tmpPath, matMap); });
  ASSERT_NE(assembly, nullptr);

  // Both components reference the same prototype => only 1 logical volume.
  EXPECT_EQ(assembly->GetLogicalVolumes().size(), 1u);

  // Imprint into a large world volume and check the two daughter placements.
  auto* worldBox  = new G4Box("TranslationTestWorld", 500.0, 500.0, 500.0);
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  auto* worldLV   = new G4LogicalVolume(worldBox, air, "TranslationTestWorldLV");
  G4ThreeVector pos;
  G4RotationMatrix rot;
  assembly->MakeImprint(worldLV, pos, &rot);

  ASSERT_EQ(worldLV->GetNoDaughters(), 2);
  double x0 = worldLV->GetDaughter(0)->GetTranslation().x();
  double x1 = worldLV->GetDaughter(1)->GetTranslation().x();
  // The two placements must differ in X by kTranslationX regardless of any
  // recentering offset that is absorbed identically into both translations.
  EXPECT_NEAR(std::abs(x1 - x0), kTranslationX, 1e-6);

  std::remove(tmpPath.c_str());
  delete assembly;
}

TEST(AssemblyVolume, GetLogicalVolumesCompleteness) {
  // Verify GetLogicalVolumes() returns exactly one entry keyed by the part name
  // for a single-shape STEP file, with the correct material pointer.
  const std::string tmpPath =
      (std::filesystem::temp_directory_path() / "test_assembly_lv_complete.step").string();
  BuildSingleBoxSTEP(tmpPath, "WidgetPart", "Iron material", 15.0, 20.0, 25.0);

  G4Material* fe = G4NistManager::Instance()->FindOrBuildMaterial("G4_Fe");
  ASSERT_NE(fe, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("WidgetPart", fe);

  G4OCCTAssemblyVolume* assembly = nullptr;
  ASSERT_NO_THROW({ assembly = G4OCCTAssemblyVolume::FromSTEP(tmpPath, matMap); });
  ASSERT_NE(assembly, nullptr);

  const auto& lvMap = assembly->GetLogicalVolumes();
  EXPECT_EQ(lvMap.size(), 1u);
  ASSERT_EQ(lvMap.count("WidgetPart"), 1u);
  EXPECT_EQ(lvMap.at("WidgetPart")->GetMaterial(), fe);

  std::remove(tmpPath.c_str());
  delete assembly;
}
