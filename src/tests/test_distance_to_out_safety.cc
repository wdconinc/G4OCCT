// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <iostream>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectDistanceToOut;
using g4occt::tests::navigation::SphereFixture;

void TestBoxInsideSafety() {
  const BoxFixture box("DistanceToOutBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  ExpectDistanceToOut("box center reaches closest x face", box.solid, box.Center(), 10.0 * mm);
  ExpectDistanceToOut("box offset point uses minimum face clearance", box.solid,
                      G4ThreeVector(2.0 * mm, 3.0 * mm, 4.0 * mm), 8.0 * mm);
}

void TestSphereInsideSafety() {
  const SphereFixture sphere("DistanceToOutSphere", 50.0 * mm);

  ExpectDistanceToOut("sphere center reaches boundary at radius", sphere.solid, sphere.Center(),
                      50.0 * mm);
  ExpectDistanceToOut("sphere offset point subtracts radial distance", sphere.solid,
                      G4ThreeVector(12.0 * mm, 0.0 * mm, 0.0 * mm), 38.0 * mm);
}

void TestCylinderInsideSafety() {
  const CylinderFixture cylinder("DistanceToOutCylinder", 25.0 * mm, 40.0 * mm);

  ExpectDistanceToOut("cylinder center reaches radial wall first", cylinder.solid,
                      cylinder.Center(), 25.0 * mm);
  ExpectDistanceToOut("cylinder radial offset reaches barrel wall", cylinder.solid,
                      G4ThreeVector(20.0 * mm, 0.0 * mm, 0.0 * mm), 5.0 * mm);
  ExpectDistanceToOut("cylinder axial offset reaches endcap", cylinder.solid,
                      G4ThreeVector(0.0 * mm, 0.0 * mm, 35.0 * mm), 5.0 * mm);
}

} // namespace

int main() {
  TestBoxInsideSafety();
  TestSphereInsideSafety();
  TestCylinderInsideSafety();

  std::cout << "\nAll test_distance_to_out_safety tests passed.\n";
  return 0;
}
