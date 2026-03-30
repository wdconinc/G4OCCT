// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

// test_sensitive_detector_map.cc
// Tests for G4OCCTSensitiveDetectorMap and G4OCCTAssemblyVolume::ApplySDMap,
// and G4OCCTAssemblyRegistry.

#include "G4OCCT/G4OCCTAssemblyRegistry.hh"
#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"
#include "G4OCCT/G4OCCTSensitiveDetectorMap.hh"

#include "G4OCCTFatalCatchGuard.hh"

#include <G4NistManager.hh>
#include <G4Step.hh>
#include <G4TouchableHistory.hh>
#include <G4VSensitiveDetector.hh>

// OCCT BRep primitives
#include <BRepPrimAPI_MakeBox.hxx>
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

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

// ── Minimal mock sensitive detector ──────────────────────────────────────────

namespace {

class MockSD : public G4VSensitiveDetector {
public:
  explicit MockSD(const std::string& name) : G4VSensitiveDetector(name) {}
  void Initialize(G4HCofThisEvent*) override {}
  G4bool ProcessHits(G4Step*, G4TouchableHistory*) override { return false; }
};

// ── STEP building helpers ────────────────────────────────────────────────────

/// Build a STEP file with two free-shape boxes with distinct sizes, names, and
/// a material attribute each.  Returns @p path.
std::string BuildTwoPartSTEP(const std::string& path, const std::string& name1,
                             const std::string& mat1, const std::string& name2,
                             const std::string& mat2) {
  Handle(TDocStd_Application) app = new TDocStd_Application;
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-CAF", doc);

  Handle(XCAFDoc_ShapeTool) shapeTool  = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_MaterialTool) matTool = XCAFDoc_DocumentTool::MaterialTool(doc->Main());

  auto addBox = [&](double sx, double sy, double sz, const std::string& pName,
                    const std::string& mName) {
    TDF_Label lbl = shapeTool->AddShape(BRepPrimAPI_MakeBox(sx, sy, sz).Shape(), Standard_False);
    TDataStd_Name::Set(lbl, pName.c_str());
    TDF_Label mLbl = matTool->AddMaterial(
        new TCollection_HAsciiString(mName.c_str()), new TCollection_HAsciiString(""), 0.0,
        new TCollection_HAsciiString(""), new TCollection_HAsciiString(""));
    matTool->SetMaterial(lbl, mLbl);
  };

  addBox(10.0, 10.0, 10.0, name1, mat1);
  addBox(20.0, 20.0, 20.0, name2, mat2);

  STEPCAFControl_Writer writer;
  writer.SetNameMode(Standard_True);
  if (writer.Transfer(doc) != Standard_True)
    throw std::runtime_error("BuildTwoPartSTEP: Transfer failed");
  if (writer.Write(path.c_str()) != IFSelect_RetDone)
    throw std::runtime_error("BuildTwoPartSTEP: Write failed to " + path);
  return path;
}

/// Build a STEP file with three free-shape boxes all named @p partName, each
/// with the same material attribute.  After import via FromSTEP,
/// MakeUniqueName deduplicates them to partName, partName_1, partName_2.
std::string BuildThreeIdenticalPartsSTEP(const std::string& path, const std::string& partName,
                                         const std::string& matName) {
  Handle(TDocStd_Application) app = new TDocStd_Application;
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-CAF", doc);

  Handle(XCAFDoc_ShapeTool) shapeTool  = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_MaterialTool) matTool = XCAFDoc_DocumentTool::MaterialTool(doc->Main());

  TDF_Label mLbl = matTool->AddMaterial(
      new TCollection_HAsciiString(matName.c_str()), new TCollection_HAsciiString(""), 0.0,
      new TCollection_HAsciiString(""), new TCollection_HAsciiString(""));

  // Three geometrically distinct boxes so XDE creates separate labels.
  double sizes[3][3] = {{10.0, 10.0, 10.0}, {20.0, 20.0, 20.0}, {30.0, 30.0, 30.0}};
  for (auto& sz : sizes) {
    TDF_Label lbl =
        shapeTool->AddShape(BRepPrimAPI_MakeBox(sz[0], sz[1], sz[2]).Shape(), Standard_False);
    TDataStd_Name::Set(lbl, partName.c_str());
    matTool->SetMaterial(lbl, mLbl);
  }

  STEPCAFControl_Writer writer;
  writer.SetNameMode(Standard_True);
  if (writer.Transfer(doc) != Standard_True)
    throw std::runtime_error("BuildThreeIdenticalPartsSTEP: Transfer failed");
  if (writer.Write(path.c_str()) != IFSelect_RetDone)
    throw std::runtime_error("BuildThreeIdenticalPartsSTEP: Write failed to " + path);
  return path;
}

/// Return a unique temporary STEP file path for the given test name.
std::string TmpStepPath(const std::string& label) {
  const std::string suffix = std::to_string(std::random_device{}());
  return (std::filesystem::temp_directory_path() / (label + "_" + suffix + ".step")).string();
}

} // namespace

// ── G4OCCTSensitiveDetectorMap unit tests ────────────────────────────────────

TEST(SensitiveDetectorMap, AddAndResolveExact) {
  MockSD sd("AbsorberSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd);
  EXPECT_EQ(sdMap.Resolve("Absorber"), &sd);
}

TEST(SensitiveDetectorMap, ResolveReturnsNullptrForUnregistered) {
  G4OCCTSensitiveDetectorMap sdMap;
  EXPECT_EQ(sdMap.Resolve("Unknown"), nullptr);
}

TEST(SensitiveDetectorMap, PrefixMatchWithDigitSuffix) {
  MockSD sd("AbsorberSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd);
  EXPECT_EQ(sdMap.Resolve("Absorber_1"), &sd);
  EXPECT_EQ(sdMap.Resolve("Absorber_2"), &sd);
}

TEST(SensitiveDetectorMap, PrefixMatchNoMatchForNonDigitSuffix) {
  MockSD sd("AbsorberSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd);
  EXPECT_EQ(sdMap.Resolve("Absorber_x"), nullptr);
  EXPECT_EQ(sdMap.Resolve("Absorber_1x"), nullptr);
}

TEST(SensitiveDetectorMap, ExactMatchTakesPrecedenceOverPrefix) {
  MockSD sd1("SD1");
  MockSD sd2("SD2");
  G4OCCTSensitiveDetectorMap sdMap;
  // Insert "Part_1" (exact) BEFORE "Part" (prefix); exact entry must win for "Part_1".
  sdMap.Add("Part_1", &sd2);
  sdMap.Add("Part", &sd1);
  // "Part_1" resolves via its own exact entry (inserted first).
  EXPECT_EQ(sdMap.Resolve("Part_1"), &sd2);
  // "Part_2" has no exact entry; matches "Part" prefix.
  EXPECT_EQ(sdMap.Resolve("Part_2"), &sd1);
  // "Part" itself resolves exactly.
  EXPECT_EQ(sdMap.Resolve("Part"), &sd1);
}

TEST(SensitiveDetectorMap, SizeReflectsEntries) {
  MockSD sd1("SD1");
  MockSD sd2("SD2");
  G4OCCTSensitiveDetectorMap sdMap;
  EXPECT_EQ(sdMap.Size(), 0u);
  sdMap.Add("Absorber", &sd1);
  EXPECT_EQ(sdMap.Size(), 1u);
  sdMap.Add("Gap", &sd2);
  EXPECT_EQ(sdMap.Size(), 2u);
}

TEST(SensitiveDetectorMap, AddNullSDIsFatal) {
  G4OCCTSensitiveDetectorMap sdMap;
  EXPECT_DEATH(sdMap.Add("X", nullptr), ".*G4Exception.*");
}

TEST(SensitiveDetectorMap, AddNullSDTriggersFatalCode) {
  G4OCCTFatalCatchGuard guard;
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("X", nullptr);
  EXPECT_TRUE(guard.catcher->caught);
  EXPECT_EQ(guard.catcher->code, "G4OCCT_SDMap000");
  EXPECT_EQ(sdMap.Size(), 0u);
}

TEST(SensitiveDetectorMap, MultiplePatterns) {
  MockSD sd1("AbsorberSD");
  MockSD sd2("GapSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd1);
  sdMap.Add("Gap", &sd2);
  EXPECT_EQ(sdMap.Resolve("Absorber"), &sd1);
  EXPECT_EQ(sdMap.Resolve("Gap"), &sd2);
  EXPECT_EQ(sdMap.Resolve("Other"), nullptr);
}

TEST(SensitiveDetectorMap, OverwriteEntry) {
  MockSD sd1("SD1");
  MockSD sd2("SD2");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd1);
  EXPECT_EQ(sdMap.Resolve("Absorber"), &sd1);
  sdMap.Add("Absorber", &sd2);
  EXPECT_EQ(sdMap.Resolve("Absorber"), &sd2);
  EXPECT_EQ(sdMap.Size(), 1u);
}

// ── G4OCCTAssemblyVolume::ApplySDMap integration tests ───────────────────────

TEST(AssemblyApplySDMap, AssignsByExactMatch) {
  const std::string path = TmpStepPath("test_sdmap_exact");
  BuildTwoPartSTEP(path, "Absorber", "G4_Al", "Gap", "G4_AIR");

  G4Material* al  = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  ASSERT_NE(al, nullptr);
  ASSERT_NE(air, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Absorber", al);
  matMap.Add("G4_Al", al);
  matMap.Add("Gap", air);
  matMap.Add("G4_AIR", air);

  std::unique_ptr<G4OCCTAssemblyVolume> assembly;
  ASSERT_NO_THROW({ assembly.reset(G4OCCTAssemblyVolume::FromSTEP(path, matMap)); });
  ASSERT_NE(assembly, nullptr);

  MockSD sd("AbsorberSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd);

  const std::size_t count = assembly->ApplySDMap(sdMap);
  EXPECT_EQ(count, 1u);

  const auto& lvMap = assembly->GetLogicalVolumes();
  ASSERT_EQ(lvMap.count("Absorber"), 1u);
  EXPECT_EQ(lvMap.at("Absorber")->GetSensitiveDetector(), &sd);

  std::filesystem::remove(path);
}

TEST(AssemblyApplySDMap, ReturnCountIsCorrect) {
  const std::string path = TmpStepPath("test_sdmap_count");
  BuildTwoPartSTEP(path, "Absorber", "G4_Al", "Gap", "G4_AIR");

  G4Material* al  = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  ASSERT_NE(al, nullptr);
  ASSERT_NE(air, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Absorber", al);
  matMap.Add("G4_Al", al);
  matMap.Add("Gap", air);
  matMap.Add("G4_AIR", air);

  std::unique_ptr<G4OCCTAssemblyVolume> assembly;
  ASSERT_NO_THROW({ assembly.reset(G4OCCTAssemblyVolume::FromSTEP(path, matMap)); });
  ASSERT_NE(assembly, nullptr);

  MockSD sd1("AbsorberSD");
  MockSD sd2("GapSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd1);
  sdMap.Add("Gap", &sd2);

  EXPECT_EQ(assembly->ApplySDMap(sdMap), 2u);

  std::filesystem::remove(path);
}

TEST(AssemblyApplySDMap, UnmatchedVolumeGetsNoSD) {
  const std::string path = TmpStepPath("test_sdmap_unmatched");
  BuildTwoPartSTEP(path, "Absorber", "G4_Al", "Gap", "G4_AIR");

  G4Material* al  = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  ASSERT_NE(al, nullptr);
  ASSERT_NE(air, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Absorber", al);
  matMap.Add("G4_Al", al);
  matMap.Add("Gap", air);
  matMap.Add("G4_AIR", air);

  std::unique_ptr<G4OCCTAssemblyVolume> assembly;
  ASSERT_NO_THROW({ assembly.reset(G4OCCTAssemblyVolume::FromSTEP(path, matMap)); });
  ASSERT_NE(assembly, nullptr);

  MockSD sd("AbsorberSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd);

  assembly->ApplySDMap(sdMap);

  const auto& lvMap = assembly->GetLogicalVolumes();
  ASSERT_EQ(lvMap.count("Gap"), 1u);
  EXPECT_EQ(lvMap.at("Gap")->GetSensitiveDetector(), nullptr);

  std::filesystem::remove(path);
}

TEST(AssemblyApplySDMap, PrefixMatchesDeduplicatedVolumes) {
  const std::string path = TmpStepPath("test_sdmap_prefix_dedup");
  BuildThreeIdenticalPartsSTEP(path, "Absorber", "G4_Al");

  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  ASSERT_NE(al, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Absorber", al);
  matMap.Add("G4_Al", al);

  std::unique_ptr<G4OCCTAssemblyVolume> assembly;
  ASSERT_NO_THROW({ assembly.reset(G4OCCTAssemblyVolume::FromSTEP(path, matMap)); });
  ASSERT_NE(assembly, nullptr);

  // MakeUniqueName deduplicates: "Absorber", "Absorber_1", "Absorber_2".
  ASSERT_EQ(assembly->GetLogicalVolumes().size(), 3u);
  ASSERT_EQ(assembly->GetLogicalVolumes().count("Absorber"), 1u);
  ASSERT_EQ(assembly->GetLogicalVolumes().count("Absorber_1"), 1u);
  ASSERT_EQ(assembly->GetLogicalVolumes().count("Absorber_2"), 1u);

  MockSD sd("AbsorberSD");
  G4OCCTSensitiveDetectorMap sdMap;
  sdMap.Add("Absorber", &sd);

  // Prefix "Absorber" matches "Absorber" (exact), "Absorber_1", "Absorber_2".
  EXPECT_EQ(assembly->ApplySDMap(sdMap), 3u);

  const auto& lvMap = assembly->GetLogicalVolumes();
  EXPECT_EQ(lvMap.at("Absorber")->GetSensitiveDetector(), &sd);
  EXPECT_EQ(lvMap.at("Absorber_1")->GetSensitiveDetector(), &sd);
  EXPECT_EQ(lvMap.at("Absorber_2")->GetSensitiveDetector(), &sd);

  std::filesystem::remove(path);
}

// ── G4OCCTAssemblyRegistry tests ─────────────────────────────────────────────

TEST(AssemblyRegistry, RegisterAndGet) {
  const std::string name = "RegistryTest_RegisterAndGet";
  auto* assembly         = new G4OCCTAssemblyVolume();
  G4OCCTAssemblyRegistry::Instance().Register(name, assembly);
  EXPECT_EQ(G4OCCTAssemblyRegistry::Instance().Get(name), assembly);
  // Clean up: release ownership and delete.
  delete G4OCCTAssemblyRegistry::Instance().Release(name);
}

TEST(AssemblyRegistry, GetUnknownReturnsNullptr) {
  EXPECT_EQ(G4OCCTAssemblyRegistry::Instance().Get("RegistryTest_Nonexistent"), nullptr);
}

TEST(AssemblyRegistry, SizeReflectsEntries) {
  const std::string name1  = "RegistryTest_Size1";
  const std::string name2  = "RegistryTest_Size2";
  const std::size_t before = G4OCCTAssemblyRegistry::Instance().Size();

  auto* a1 = new G4OCCTAssemblyVolume();
  auto* a2 = new G4OCCTAssemblyVolume();

  G4OCCTAssemblyRegistry::Instance().Register(name1, a1);
  EXPECT_EQ(G4OCCTAssemblyRegistry::Instance().Size(), before + 1u);

  G4OCCTAssemblyRegistry::Instance().Register(name2, a2);
  EXPECT_EQ(G4OCCTAssemblyRegistry::Instance().Size(), before + 2u);

  delete G4OCCTAssemblyRegistry::Instance().Release(name1);
  delete G4OCCTAssemblyRegistry::Instance().Release(name2);
  EXPECT_EQ(G4OCCTAssemblyRegistry::Instance().Size(), before);
}

TEST(AssemblyRegistry, ReleaseReturnsAndRemoves) {
  const std::string name = "RegistryTest_Release";
  auto* assembly         = new G4OCCTAssemblyVolume();
  G4OCCTAssemblyRegistry::Instance().Register(name, assembly);

  G4OCCTAssemblyVolume* released = G4OCCTAssemblyRegistry::Instance().Release(name);
  EXPECT_EQ(released, assembly);
  EXPECT_EQ(G4OCCTAssemblyRegistry::Instance().Get(name), nullptr);
  delete released;
}
