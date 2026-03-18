// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectInsideClassification;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::ExpectTrue;
using g4occt::tests::navigation::ExpectVectorNear;
using g4occt::tests::navigation::SphereFixture;

TEST(NavigationHarness, CanonicalBoxFixture) {
  const BoxFixture box("HarnessBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  ExpectTrue("box fixture creates a shape", !box.shape.IsNull());
  ExpectTrue("box solid stores a shape", !box.solid.GetOCCTShape().IsNull());
  ExpectNear("box +x surface point uses half-length", box.PositiveXSurface().x(), 10.0 * mm);
  ExpectNear("box +y surface point uses half-length", box.PositiveYSurface().y(), 20.0 * mm);
  ExpectNear("box +z surface point uses half-length", box.PositiveZSurface().z(), 30.0 * mm);
  ExpectVectorNear("box center is origin", box.Center(), G4ThreeVector(0.0, 0.0, 0.0));
}

TEST(NavigationHarness, CanonicalSphereFixture) {
  const SphereFixture sphere("HarnessSphere", 50.0 * mm);

  ExpectTrue("sphere fixture creates a shape", !sphere.shape.IsNull());
  ExpectTrue("sphere solid stores a shape", !sphere.solid.GetOCCTShape().IsNull());
  ExpectNear("sphere +x surface point uses radius", sphere.PositiveXSurface().x(), 50.0 * mm);
  ExpectNear("sphere outside point uses margin", sphere.OutsideX().x(), 55.0 * mm);
}

TEST(NavigationHarness, CanonicalCylinderFixture) {
  const CylinderFixture cylinder("HarnessCylinder", 25.0 * mm, 40.0 * mm);

  ExpectTrue("cylinder fixture creates a shape", !cylinder.shape.IsNull());
  ExpectTrue("cylinder solid stores a shape", !cylinder.solid.GetOCCTShape().IsNull());
  ExpectNear("cylinder radial surface uses radius", cylinder.PositiveRadialSurface().x(),
             25.0 * mm);
  ExpectNear("cylinder top surface uses half-length", cylinder.PositiveZSurface().z(), 40.0 * mm);
  ExpectNear("cylinder outside radial point uses margin", cylinder.OutsideRadialX().x(), 30.0 * mm);
}

TEST(NavigationHarness, AssertionHelpers) {
  ExpectNear("finite comparison accepts tolerance", 1.0 * mm + 5.0e-10 * mm, 1.0 * mm);
  ExpectNear("infinite comparison accepts kInfinity", kInfinity, kInfinity);
  ExpectVectorNear("vector comparison accepts tolerance",
                   G4ThreeVector(1.0 * mm + 5.0e-10 * mm, 2.0 * mm, 3.0 * mm),
                   G4ThreeVector(1.0 * mm, 2.0 * mm, 3.0 * mm));
  ExpectInsideClassification("inside classification helper compares enums", kSurface, kSurface);
}

} // namespace
