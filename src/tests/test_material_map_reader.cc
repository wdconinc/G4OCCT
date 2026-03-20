// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

// test_material_map_reader.cc
// Tests for G4OCCTMaterialMapReader: verify XML file parsing into
// G4OCCTMaterialMap for both NIST aliases and inline GDML material
// definitions.

#include "G4OCCT/G4OCCTMaterialMapReader.hh"

#include <G4Material.hh>
#include <G4NistManager.hh>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

/// Write @p content to a temporary file and return its path.
std::string WriteTempXML(const std::string& filename, const std::string& content) {
  const std::string path =
      (std::filesystem::temp_directory_path() / filename).string();
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
  // Simple single-element material (liquid argon — not a NIST material at
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
  EXPECT_DEATH(reader.ReadFile(path), ".*stepName.*");
}

TEST(MaterialMapReader, UnknownNistNameIsFatal) {
  const std::string path = WriteTempXML("test_mmr_bad_nist.xml", R"xml(
<materials>
  <material stepName="Mystery" geant4Name="G4_DOES_NOT_EXIST_XYZ"/>
</materials>
)xml");

  G4OCCTMaterialMapReader reader;
  EXPECT_DEATH(reader.ReadFile(path), ".*G4_DOES_NOT_EXIST_XYZ.*");
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
  EXPECT_DEATH(reader.ReadFile(path), ".*name.*");
}
