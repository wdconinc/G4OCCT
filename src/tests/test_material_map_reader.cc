// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

// test_material_map_reader.cc
// Tests for G4OCCTMaterialMapReader: verify XML file parsing into
// G4OCCTMaterialMap for both NIST aliases and inline GDML material
// definitions.

#include "G4OCCT/G4OCCTMaterialMapReader.hh"

#include "G4OCCTFatalCatchGuard.hh"

#include <G4Material.hh>
#include <G4NistManager.hh>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace {

/// Write @p content to a temporary file and return its path.
std::string WriteTempXML(const std::string& filename, const std::string& content) {
  const std::string path = (std::filesystem::temp_directory_path() / filename).string();
  std::ofstream out(path);
  out << content;
  return path;
}

} // namespace

// ── NIST alias ────────────────────────────────────────────────────────────────

TEST(MaterialMapReader, NistAliasSingleEntry) {
  const std::string path = WriteTempXML("test_mmr_nist_single.xml", R"xml(
<materials>
  <material stepName="Al 6061" geant4Name="G4_Al"/>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);

  EXPECT_EQ(map.Size(), 1u);
  ASSERT_TRUE(map.Contains("Al 6061"));

  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  EXPECT_EQ(map.Resolve("Al 6061"), al);
}

TEST(MaterialMapReader, NistAliasMultipleEntries) {
  const std::string path = WriteTempXML("test_mmr_nist_multi.xml", R"xml(
<materials>
  <material stepName="AISI 316L" geant4Name="G4_STAINLESS-STEEL"/>
  <material stepName="Al 6061"   geant4Name="G4_Al"/>
  <material stepName="Copper"    geant4Name="G4_Cu"/>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);

  EXPECT_EQ(map.Size(), 3u);
  EXPECT_TRUE(map.Contains("AISI 316L"));
  EXPECT_TRUE(map.Contains("Al 6061"));
  EXPECT_TRUE(map.Contains("Copper"));

  G4Material* ss = G4NistManager::Instance()->FindOrBuildMaterial("G4_STAINLESS-STEEL");
  EXPECT_EQ(map.Resolve("AISI 316L"), ss);
}

// ── Inline GDML material definitions ─────────────────────────────────────────

TEST(MaterialMapReader, InlineSingleElementMaterial) {
  // Simple single-element material (liquid argon -- not a NIST material at
  // the correct density, so must be defined inline).
  const std::string path = WriteTempXML("test_mmr_inline_single.xml", R"xml(
<materials>
  <material stepName="liquidArgon" name="liquidArgon" state="liquid">
    <D value="1.390" unit="g/cm3"/>
    <fraction n="1.0" ref="G4_Ar"/>
  </material>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);

  EXPECT_EQ(map.Size(), 1u);
  ASSERT_TRUE(map.Contains("liquidArgon"));

  G4Material* mat = map.Resolve("liquidArgon");
  ASSERT_NE(mat, nullptr);
  // Density as specified.
  EXPECT_NEAR(mat->GetDensity() / (CLHEP::g / CLHEP::cm3), 1.390, 1e-6);
}

TEST(MaterialMapReader, InlineMixtureWithExplicitElement) {
  // FR4 approximation — inline element + inline material mixture.
  const std::string path = WriteTempXML("test_mmr_fr4.xml", R"xml(
<materials>
  <material stepName="FR4" name="FR4" state="solid">
    <D value="1.86" unit="g/cm3"/>
    <fraction n="0.18" ref="G4_Si"/>
    <fraction n="0.39" ref="G4_O"/>
    <fraction n="0.28" ref="G4_C"/>
    <fraction n="0.03" ref="G4_H"/>
    <fraction n="0.12" ref="G4_Br"/>
  </material>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);

  ASSERT_TRUE(map.Contains("FR4"));
  G4Material* fr4 = map.Resolve("FR4");
  ASSERT_NE(fr4, nullptr);
  EXPECT_NEAR(fr4->GetDensity() / (CLHEP::g / CLHEP::cm3), 1.86, 1e-4);
  EXPECT_EQ(fr4->GetNumberOfElements(), 5u);
}

// ── Mixed NIST and inline ─────────────────────────────────────────────────────

TEST(MaterialMapReader, MixedNistAndInline) {
  const std::string path = WriteTempXML("test_mmr_mixed.xml", R"xml(
<materials>
  <material stepName="Lead"        geant4Name="G4_Pb"/>
  <material stepName="liquidArgon" name="liquidArgon" state="liquid">
    <D value="1.390" unit="g/cm3"/>
    <fraction n="1.0" ref="G4_Ar"/>
  </material>
  <material stepName="Vacuum"      geant4Name="G4_Galactic"/>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);

  EXPECT_EQ(map.Size(), 3u);
  EXPECT_TRUE(map.Contains("Lead"));
  EXPECT_TRUE(map.Contains("liquidArgon"));
  EXPECT_TRUE(map.Contains("Vacuum"));

  G4Material* pb = G4NistManager::Instance()->FindOrBuildMaterial("G4_Pb");
  EXPECT_EQ(map.Resolve("Lead"), pb);
}

// ── Error cases ───────────────────────────────────────────────────────────────

TEST(MaterialMapReader, MissingStepNameIsFatal) {
  const std::string path = WriteTempXML("test_mmr_no_stepname.xml", R"xml(
<materials>
  <material geant4Name="G4_Al"/>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  // A malformed material definition should trigger a fatal G4Exception,
  // causing the process to terminate. Check that invoking ReadFile results
  // in death with output mentioning G4Exception.
  EXPECT_DEATH(reader.ReadFile(path), ".*G4Exception.*");
}

TEST(MaterialMapReader, UnknownNistNameIsFatal) {
  const std::string path = WriteTempXML("test_mmr_bad_nist.xml", R"xml(
<materials>
  <material stepName="Mystery" geant4Name="G4_DOES_NOT_EXIST_XYZ"/>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  EXPECT_DEATH(reader.ReadFile(path), ".*G4Exception.*");
}

TEST(MaterialMapReader, InlineWithoutNameIsFatal) {
  const std::string path = WriteTempXML("test_mmr_inline_no_name.xml", R"xml(
<materials>
  <material stepName="SomeMat" state="solid">
    <D value="1.0" unit="g/cm3"/>
    <fraction n="1.0" ref="G4_Al"/>
  </material>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  EXPECT_DEATH(reader.ReadFile(path), ".*G4Exception.*");
}

// ── Additional error / branch coverage ───────────────────────────────────────

TEST(MaterialMapReader, MissingFileTriggersXMLException) {
  // A non-existent path causes Xerces to throw XMLException, caught as
  // G4OCCT_MatReader001.
  const std::filesystem::path missing_path =
      std::filesystem::temp_directory_path() / "test_mmr_missing_file.xml";
  // Ensure the path does not exist before invoking the reader.
  std::filesystem::remove(missing_path);

  G4OCCTMaterialMapReader reader;
  EXPECT_DEATH(reader.ReadFile(missing_path.string()), ".*G4Exception.*");
}

TEST(MaterialMapReader, WrongRootTagIsFatal) {
  // Root element is not <materials> → G4OCCT_MatReader005.
  const std::string path = WriteTempXML(
      "test_mmr_wrong_root.xml",
      R"xml(<?xml version="1.0"?><root><material stepName="X" geant4Name="G4_Al"/></root>)xml");
  G4OCCTMaterialMapReader reader;
  EXPECT_DEATH(reader.ReadFile(path), ".*G4Exception.*");
}

TEST(MaterialMapReader, IsotopeAndElementBranchesAreTraversed) {
  // Exercise the tag == "isotope" and tag == "element" branches in Pass 1.
  // The NIST alias in Pass 2 keeps the test non-fatal.
  const std::string path = WriteTempXML("test_mmr_isotope_element.xml", R"xml(<?xml version="1.0"?>
<materials>
  <isotope name="G4OCCT_UniqueIso_H1" Z="1" N="1">
    <atom type="A" value="1.00782503207"/>
  </isotope>
  <element name="G4OCCT_UniqueElem_H" Z="1" formula="H">
    <atom value="1.00794"/>
  </element>
  <material stepName="IsoElemAlTest" geant4Name="G4_Al"/>
</materials>
)xml");
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map;
  ASSERT_NO_FATAL_FAILURE(map = reader.ReadFile(path));
  EXPECT_EQ(map.Size(), 1u);
  EXPECT_TRUE(map.Contains("IsoElemAlTest"));
}

TEST(MaterialMapReader, InlineMaterialReuseWhenAlreadyRegistered) {
  // Pre-register a G4Material in the global table, then verify the reader
  // reuses it (the GetMaterial(gdmlName) reuse branch) instead of re-creating.
  const G4String baseName = "G4OCCT_ReuseMat_TestUnique";
  // Make the material name unique per test run to avoid polluting the global table.
  std::ostringstream nameBuilder;
  nameBuilder << baseName << "_" << std::random_device{}();
  const G4String matName = nameBuilder.str();
  G4Material* premat     = G4Material::GetMaterial(matName, /*warning=*/false);
  if (premat == nullptr) {
    premat = new G4Material(matName, 18.0, 39.948 * CLHEP::g / CLHEP::mole,
                            1.39 * CLHEP::g / CLHEP::cm3, kStateLiquid);
  }
  ASSERT_NE(premat, nullptr);

  const std::string path = WriteTempXML(
      "test_mmr_reuse.xml",
      std::string("<?xml version=\"1.0\"?>\n"
                  "<materials>\n"
                  "  <material stepName=\"reuseStep\" name=\"") +
          matName +
          std::string("\" state=\"liquid\">\n"
                      "    <D value=\"1.39\" unit=\"g/cm3\"/>\n"
                      "    <fraction n=\"1.0\" ref=\"G4_Ar\"/>\n"
                      "  </material>\n"
                      "</materials>\n"));
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map;
  ASSERT_NO_FATAL_FAILURE(map = reader.ReadFile(path));
  ASSERT_TRUE(map.Contains("reuseStep"));
  // The resolved pointer must be the pre-registered material, not a new copy.
  EXPECT_EQ(map.Resolve("reuseStep"), premat);
}

// -- Fatal-path coverage (G4OCCTFatalCatchGuard) -----------------------------------
// The tests below exercise fatal G4Exception paths in-process by temporarily
// installing a G4OCCTFatalCatcher handler that returns false (= don't abort).
// This approach contributes to gcov line/branch coverage, complementing the
// EXPECT_DEATH variants above which verify aborting behaviour but run in a
// forked child and therefore do not accumulate coverage data.

TEST(MaterialMapReader, MissingFileTriggersFatalCode) {
  // A non-existent path causes Xerces to throw XMLException, which is caught
  // and rethrown as G4OCCT_MatReader001 (FatalException).
  const std::filesystem::path missing_path =
      std::filesystem::temp_directory_path() / "test_mmr_fatal_missing.xml";
  std::filesystem::remove(missing_path);

  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(missing_path.string());
  EXPECT_TRUE(guard.catcher->caught);
  EXPECT_EQ(guard.catcher->code, "G4OCCT_MatReader001");
  EXPECT_EQ(map.Size(), 0u); // empty result returned after non-aborting exception
}

TEST(MaterialMapReader, WrongRootTagTriggersFatalCode) {
  // Root element is not <materials> → G4OCCT_MatReader005.
  const std::string path = WriteTempXML(
      "test_mmr_fatal_wrong_root.xml",
      R"xml(<?xml version="1.0"?><root><material stepName="X" geant4Name="G4_Al"/></root>)xml");

  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);
  EXPECT_TRUE(guard.catcher->caught);
  EXPECT_EQ(guard.catcher->code, "G4OCCT_MatReader005");
  EXPECT_EQ(map.Size(), 0u);
}

TEST(MaterialMapReader, MissingStepNameTriggersFatalCode) {
  // A <material> element without stepName → G4OCCT_MatReader006.
  const std::string path = WriteTempXML("test_mmr_fatal_no_stepname.xml", R"xml(
<materials>
  <material geant4Name="G4_Al"/>
</materials>
)xml");

  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);
  EXPECT_TRUE(guard.catcher->caught);
  EXPECT_EQ(guard.catcher->code, "G4OCCT_MatReader006");
  EXPECT_EQ(map.Size(), 0u);
}

TEST(MaterialMapReader, UnknownNistNameTriggersFatalCode) {
  // A geant4Name that does not exist in the NIST database → G4OCCT_MatReader007.
  const std::string path = WriteTempXML("test_mmr_fatal_bad_nist.xml", R"xml(
<materials>
  <material stepName="Mystery" geant4Name="G4_DOES_NOT_EXIST_XYZ_FATAL"/>
</materials>
)xml");

  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);
  EXPECT_TRUE(guard.catcher->caught);
  EXPECT_EQ(guard.catcher->code, "G4OCCT_MatReader007");
  EXPECT_EQ(map.Size(), 0u);
}

TEST(MaterialMapReader, InlineWithoutNameTriggersFatalCode) {
  // Inline material with stepName but no name attribute → G4OCCT_MatReader008.
  const std::string path = WriteTempXML("test_mmr_fatal_inline_no_name.xml", R"xml(
<materials>
  <material stepName="SomeMat" state="solid">
    <D value="1.0" unit="g/cm3"/>
    <fraction n="1.0" ref="G4_Al"/>
  </material>
</materials>
)xml");

  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMapReader reader;
  G4OCCTMaterialMap map = reader.ReadFile(path);
  EXPECT_TRUE(guard.catcher->caught);
  EXPECT_EQ(guard.catcher->code, "G4OCCT_MatReader008");
  EXPECT_EQ(map.Size(), 0u);
}
