// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::ExpectDistanceToIn;

TEST(DistanceToInRay, HitReturnsEntryDistance) {
  const BoxFixture box("DistanceToInRayHitBox");

  ExpectDistanceToIn("ray hit returns distance to +x face", box.solid, box.OutsideX(),
                     G4ThreeVector(-1.0, 0.0, 0.0), 5.0 * mm);
}

TEST(DistanceToInRay, MissReturnsInfinity) {
  const BoxFixture box("DistanceToInRayMissBox");

  ExpectDistanceToIn("ray miss returns kInfinity", box.solid, box.OutsideX(),
                     G4ThreeVector(1.0, 0.0, 0.0), kInfinity);
}

TEST(DistanceToInRay, ChoosesFirstPositiveIntersection) {
  const BoxFixture box("DistanceToInRayFirstHitBox");
  const G4ThreeVector start(-box.halfX - 15.0 * mm, 0.0, 0.0);

  ExpectDistanceToIn("ray chooses first positive hit", box.solid, start,
                     G4ThreeVector(1.0, 0.0, 0.0), 15.0 * mm);
}

} // namespace
