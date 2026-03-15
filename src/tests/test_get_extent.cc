// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4VisExtent.hh>

#include <iostream>
#include <string>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::SphereFixture;

void ExpectExtent(const std::string& label, const G4VisExtent& extent, const G4double xMin,
                  const G4double xMax, const G4double yMin, const G4double yMax,
                  const G4double zMin, const G4double zMax,
                  const G4double tolerance = 1.0e-6 * mm) {
  ExpectNear(label + " xmin", extent.GetXmin(), xMin, tolerance);
  ExpectNear(label + " xmax", extent.GetXmax(), xMax, tolerance);
  ExpectNear(label + " ymin", extent.GetYmin(), yMin, tolerance);
  ExpectNear(label + " ymax", extent.GetYmax(), yMax, tolerance);
  ExpectNear(label + " zmin", extent.GetZmin(), zMin, tolerance);
  ExpectNear(label + " zmax", extent.GetZmax(), zMax, tolerance);
}

void TestBoxExtent() {
  const BoxFixture box("ExtentBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  ExpectExtent("centered box extent", box.solid.GetExtent(), -10.0 * mm, 10.0 * mm, -20.0 * mm,
               20.0 * mm, -30.0 * mm, 30.0 * mm);
}

void TestSphereExtent() {
  const SphereFixture sphere("ExtentSphere", 50.0 * mm);
  ExpectExtent("centered sphere extent", sphere.solid.GetExtent(), -50.0 * mm, 50.0 * mm,
               -50.0 * mm, 50.0 * mm, -50.0 * mm, 50.0 * mm);
}

void TestCylinderExtent() {
  const CylinderFixture cylinder("ExtentCylinder", 25.0 * mm, 40.0 * mm);
  ExpectExtent("centered cylinder extent", cylinder.solid.GetExtent(), -25.0 * mm, 25.0 * mm,
               -25.0 * mm, 25.0 * mm, -40.0 * mm, 40.0 * mm);
}

} // namespace

int main() {
  TestBoxExtent();
  TestSphereExtent();
  TestCylinderExtent();

  std::cout << "\nAll test_get_extent tests passed.\n";
  return 0;
}
