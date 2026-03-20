// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <cmath>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::ExpectSurfaceNormal;
using g4occt::tests::navigation::SphereFixture;

void ExpectUnitNormal(const std::string& label, const G4ThreeVector& normal) {
  ExpectNear(label, normal.mag(), 1.0, 1.0e-12);
}

TEST(SurfaceNormal, Box) {
  const BoxFixture box("SurfaceNormalBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  const G4ThreeVector positiveXNormal = box.solid.SurfaceNormal(box.PositiveXSurface());
  ExpectSurfaceNormal("box +x face normal", box.solid, box.PositiveXSurface(),
                      G4ThreeVector(1.0, 0.0, 0.0));
  ExpectUnitNormal("box +x face normal is unit length", positiveXNormal);

  const G4ThreeVector negativeYPoint(0.0, -box.halfY, 0.0);
  const G4ThreeVector negativeYNormal = box.solid.SurfaceNormal(negativeYPoint);
  ExpectSurfaceNormal("box -y face normal", box.solid, negativeYPoint,
                      G4ThreeVector(0.0, -1.0, 0.0));
  ExpectUnitNormal("box -y face normal is unit length", negativeYNormal);

  const G4ThreeVector positiveZNormal = box.solid.SurfaceNormal(box.PositiveZSurface());
  ExpectSurfaceNormal("box +z face normal", box.solid, box.PositiveZSurface(),
                      G4ThreeVector(0.0, 0.0, 1.0));
  ExpectUnitNormal("box +z face normal is unit length", positiveZNormal);
}

TEST(SurfaceNormal, Sphere) {
  const SphereFixture sphere("SurfaceNormalSphere", 50.0 * mm);

  const G4double component = sphere.radius / std::sqrt(3.0);
  const G4ThreeVector diagonalPoint(component, component, component);
  const G4ThreeVector expectedDiagonalNormal = diagonalPoint.unit();
  const G4ThreeVector diagonalNormal         = sphere.solid.SurfaceNormal(diagonalPoint);
  ExpectSurfaceNormal("sphere diagonal normal", sphere.solid, diagonalPoint,
                      expectedDiagonalNormal);
  ExpectUnitNormal("sphere diagonal normal is unit length", diagonalNormal);

  const G4double equatorialComponent = sphere.radius / std::sqrt(2.0);
  const G4ThreeVector equatorialPoint(0.0, equatorialComponent, equatorialComponent);
  const G4ThreeVector expectedEquatorialNormal = equatorialPoint.unit();
  const G4ThreeVector equatorialNormal         = sphere.solid.SurfaceNormal(equatorialPoint);
  ExpectSurfaceNormal("sphere equatorial normal", sphere.solid, equatorialPoint,
                      expectedEquatorialNormal);
  ExpectUnitNormal("sphere equatorial normal is unit length", equatorialNormal);
}

TEST(SurfaceNormal, Cylinder) {
  const CylinderFixture cylinder("SurfaceNormalCylinder", 25.0 * mm, 40.0 * mm);

  const G4double radialComponent = cylinder.radius / std::sqrt(2.0);
  const G4ThreeVector radialPoint(radialComponent, radialComponent, 0.0);
  const G4ThreeVector expectedRadialNormal = radialPoint.unit();
  const G4ThreeVector radialNormal         = cylinder.solid.SurfaceNormal(radialPoint);
  ExpectSurfaceNormal("cylinder radial normal", cylinder.solid, radialPoint, expectedRadialNormal);
  ExpectUnitNormal("cylinder radial normal is unit length", radialNormal);

  const G4ThreeVector topNormal = cylinder.solid.SurfaceNormal(cylinder.PositiveZSurface());
  ExpectSurfaceNormal("cylinder top normal", cylinder.solid, cylinder.PositiveZSurface(),
                      G4ThreeVector(0.0, 0.0, 1.0));
  ExpectUnitNormal("cylinder top normal is unit length", topNormal);

  const G4ThreeVector bottomPoint(0.0, 0.0, -cylinder.halfLength);
  const G4ThreeVector bottomNormal = cylinder.solid.SurfaceNormal(bottomPoint);
  ExpectSurfaceNormal("cylinder bottom normal", cylinder.solid, bottomPoint,
                      G4ThreeVector(0.0, 0.0, -1.0));
  ExpectUnitNormal("cylinder bottom normal is unit length", bottomNormal);
}

} // namespace
