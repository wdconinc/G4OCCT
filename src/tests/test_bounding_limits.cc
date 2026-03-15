// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <iostream>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectVectorNear;
using g4occt::tests::navigation::SphereFixture;

constexpr G4double kBoundingTolerance = 1.0e-6 * mm;

void TestBoxBoundingLimits() {
  const BoxFixture box("BoundingBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  G4ThreeVector min;
  G4ThreeVector max;
  box.solid.BoundingLimits(min, max);

  ExpectVectorNear("box minimum bound matches half lengths", min,
                   G4ThreeVector(-10.0 * mm, -20.0 * mm, -30.0 * mm), kBoundingTolerance);
  ExpectVectorNear("box maximum bound matches half lengths", max,
                   G4ThreeVector(10.0 * mm, 20.0 * mm, 30.0 * mm), kBoundingTolerance);
}

void TestSphereBoundingLimits() {
  const SphereFixture sphere("BoundingSphere", 50.0 * mm);

  G4ThreeVector min;
  G4ThreeVector max;
  sphere.solid.BoundingLimits(min, max);

  ExpectVectorNear("sphere minimum bound matches radius", min,
                   G4ThreeVector(-50.0 * mm, -50.0 * mm, -50.0 * mm), kBoundingTolerance);
  ExpectVectorNear("sphere maximum bound matches radius", max,
                   G4ThreeVector(50.0 * mm, 50.0 * mm, 50.0 * mm), kBoundingTolerance);
}

void TestCylinderBoundingLimits() {
  const CylinderFixture cylinder("BoundingCylinder", 25.0 * mm, 40.0 * mm);

  G4ThreeVector min;
  G4ThreeVector max;
  cylinder.solid.BoundingLimits(min, max);

  ExpectVectorNear("cylinder minimum bound matches radius and half length", min,
                   G4ThreeVector(-25.0 * mm, -25.0 * mm, -40.0 * mm), kBoundingTolerance);
  ExpectVectorNear("cylinder maximum bound matches radius and half length", max,
                   G4ThreeVector(25.0 * mm, 25.0 * mm, 40.0 * mm), kBoundingTolerance);
}

} // namespace

int main() {
  TestBoxBoundingLimits();
  TestSphereBoundingLimits();
  TestCylinderBoundingLimits();

  std::cout << "\nAll test_bounding_limits tests passed.\n";
  return 0;
}
