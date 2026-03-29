// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

// test_material_map.cc
// Tests for G4OCCTMaterialMap: verify Add, Resolve, and Contains.

#include "G4OCCT/G4OCCTMaterialMap.hh"

#include "G4OCCTFatalCatchGuard.hh"

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

TEST(MaterialMap, AddNullMaterialIsFatal) {
  G4OCCTMaterialMap matMap;
  EXPECT_DEATH(matMap.Add("MyMat", nullptr), ".*G4Exception.*");
}

TEST(MaterialMap, ResolveUnregisteredNameIsFatal) {
  G4OCCTMaterialMap matMap;
  EXPECT_DEATH(matMap.Resolve("not_in_map"), ".*G4Exception.*");
}

TEST(MaterialMap, MergeCombinesTwoMaps) {
  G4Material* al = G4NistManager::Instance()->FindOrBuildMaterial("G4_Al");
  G4Material* cu = G4NistManager::Instance()->FindOrBuildMaterial("G4_Cu");
  G4Material* fe = G4NistManager::Instance()->FindOrBuildMaterial("G4_Fe");
  ASSERT_NE(al, nullptr);
  ASSERT_NE(cu, nullptr);
  ASSERT_NE(fe, nullptr);

  G4OCCTMaterialMap mapA;
  mapA.Add("Al", al);
  mapA.Add("Cu", cu);

  G4OCCTMaterialMap mapB;
  mapB.Add("Fe", fe);
  mapB.Merge(mapA);

  EXPECT_EQ(mapB.Size(), 3u);
  EXPECT_EQ(mapB.Resolve("Al"), al);
  EXPECT_EQ(mapB.Resolve("Cu"), cu);
  EXPECT_EQ(mapB.Resolve("Fe"), fe);
}

// ── Fatal-path coverage ───────────────────────────────────────────────────────
// The tests below exercise fatal G4Exception paths in-process by temporarily
// installing a FatalCatcher handler that returns false (= don't abort).
// This approach contributes to gcov line/branch coverage, complementing the
// EXPECT_DEATH variants above which verify the aborting behaviour but run in
// a forked child and therefore do not accumulate coverage data.

TEST(MaterialMap, AddNullMaterialTriggersFatalCode) {
  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMap matMap;
  matMap.Add("MyMat", nullptr);
  EXPECT_TRUE(guard.catcher.caught);
  EXPECT_EQ(guard.catcher.code, "G4OCCT_MatMap000");
  // After the non-aborting G4Exception the unreachable return executes;
  // the map must remain empty.
  EXPECT_EQ(matMap.Size(), 0u);
}

TEST(MaterialMap, ResolveUnregisteredNameTriggersFatalCode) {
  G4OCCTFatalCatchGuard guard;
  G4OCCTMaterialMap matMap;
  G4Material* result = matMap.Resolve("not_registered");
  EXPECT_TRUE(guard.catcher.caught);
  EXPECT_EQ(guard.catcher.code, "G4OCCT_MatMap001");
  EXPECT_EQ(result, nullptr); // unreachable return value
}
