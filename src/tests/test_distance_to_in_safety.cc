// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4GeometryTolerance.hh>
#include <G4SystemOfUnits.hh>

#include <algorithm>
#include <iostream>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectDistanceToIn;
using g4occt::tests::navigation::SphereFixture;

void TestShortestDistanceForExternalPoints() {
  const BoxFixture box("DistanceToInSafetyBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  ExpectDistanceToIn("box axis-aligned outside point uses face distance", box.solid,
                     box.OutsideX(7.0 * mm), 7.0 * mm);

  const G4ThreeVector edgePoint(box.halfX + 3.0 * mm, box.halfY + 4.0 * mm,
                                0.0 * mm);
  ExpectDistanceToIn("box outside point near an edge uses Euclidean shortest distance",
                     box.solid, edgePoint, 5.0 * mm);

  const G4ThreeVector cornerPoint(box.halfX + 2.0 * mm, box.halfY + 3.0 * mm,
                                  box.halfZ + 6.0 * mm);
  ExpectDistanceToIn("box outside point near a corner uses 3D Euclidean shortest distance",
                     box.solid, cornerPoint, 7.0 * mm);

  const SphereFixture sphere("DistanceToInSafetySphere", 50.0 * mm);
  ExpectDistanceToIn("sphere outside point uses radial distance", sphere.solid,
                     sphere.OutsideX(9.0 * mm), 9.0 * mm);

  const CylinderFixture cylinder("DistanceToInSafetyCylinder", 25.0 * mm, 40.0 * mm);
  ExpectDistanceToIn("cylinder outside radial point uses radial distance",
                     cylinder.solid, cylinder.OutsideRadialX(6.0 * mm), 6.0 * mm);
}

void TestNearSurfaceBehaviour() {
  const BoxFixture box("DistanceToInSafetyNearSurface", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const G4double surfaceTolerance =
      G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();

  ExpectDistanceToIn("point exactly on the surface has zero safety", box.solid,
                     box.PositiveXSurface(), 0.0);

  const G4ThreeVector withinSurfaceShell(box.halfX + 0.25 * surfaceTolerance, 0.0,
                                         0.0);
  ExpectDistanceToIn("point within the Geant4 surface shell has zero safety",
                     box.solid, withinSurfaceShell, 0.0);

  const G4double externalOffset = std::max(10.0 * surfaceTolerance, 1.0e-6 * mm);
  const G4ThreeVector outsideSurfaceShell(box.halfX + externalOffset, 0.0, 0.0);
  ExpectDistanceToIn("point just outside the surface shell returns a finite distance",
                     box.solid, outsideSurfaceShell, externalOffset);
}

} // namespace

int main() {
  TestShortestDistanceForExternalPoints();
  TestNearSurfaceBehaviour();

  std::cout << "\nAll test_distance_to_in_safety tests passed.\n";
  return 0;
}
