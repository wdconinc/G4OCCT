// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// test_material_map.cc
// Tests for G4OCCTMaterialMap: verify Add, Resolve, and Contains.

#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <G4NistManager.hh>

#include <gtest/gtest.h>

TEST(MaterialMap, AddAndResolve) {
  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  ASSERT_NE(al, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("Al 6061-T6", al);

  EXPECT_EQ(matMap.Resolve("Al 6061-T6"), al);
}

TEST(MaterialMap, ContainsReturnsTrueAfterAdd) {
  G4Material* fe = G4NistManager::Instance()->FindOrBuildMaterial("G4_Fe");
  ASSERT_NE(fe, nullptr);

  G4OCCTMaterialMap matMap;
  EXPECT_FALSE(matMap.Contains("Steel"));
  matMap.Add("Steel", fe);
  EXPECT_TRUE(matMap.Contains("Steel"));
}

TEST(MaterialMap, SizeReflectsEntries) {
  G4OCCTMaterialMap matMap;
  EXPECT_EQ(matMap.Size(), 0u);

  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  matMap.Add("Air", air);
  EXPECT_EQ(matMap.Size(), 1u);

  G4Material* cu = G4NistManager::Instance()->FindOrBuildMaterial("G4_Cu");
  matMap.Add("Copper", cu);
  EXPECT_EQ(matMap.Size(), 2u);
}

TEST(MaterialMap, OverwriteEntry) {
  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  G4Material* cu = G4NistManager::Instance()->FindOrBuildMaterial("G4_Cu");
  ASSERT_NE(al, nullptr);
  ASSERT_NE(cu, nullptr);

  G4OCCTMaterialMap matMap;
  matMap.Add("MyMat", al);
  EXPECT_EQ(matMap.Resolve("MyMat"), al);

  // Overwrite with a different material; size must not grow.
  matMap.Add("MyMat", cu);
  EXPECT_EQ(matMap.Resolve("MyMat"), cu);
  EXPECT_EQ(matMap.Size(), 1u);
}

TEST(MaterialMap, MultipleMaterials) {
  G4OCCTMaterialMap matMap;

  G4Material* al  = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  G4Material* cu  = G4NistManager::Instance()->FindOrBuildMaterial("G4_Cu");
  G4Material* air = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

  matMap.Add("Aluminium", al);
  matMap.Add("Copper", cu);
  matMap.Add("Air", air);

  EXPECT_EQ(matMap.Resolve("Aluminium"), al);
  EXPECT_EQ(matMap.Resolve("Copper"), cu);
  EXPECT_EQ(matMap.Resolve("Air"), air);
  EXPECT_EQ(matMap.Size(), 3u);
}
