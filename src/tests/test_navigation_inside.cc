// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <gtest/gtest.h>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectInside;
using g4occt::tests::navigation::SphereFixture;

TEST(InsideClassification, Box) {
  const BoxFixture box("InsideBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  ExpectInside("box center is inside", box.solid, box.Center(), kInside);
  ExpectInside("box +x face is on the surface", box.solid, box.PositiveXSurface(), kSurface);
  ExpectInside("box +y face is on the surface", box.solid, box.PositiveYSurface(), kSurface);
  ExpectInside("box +z face is on the surface", box.solid, box.PositiveZSurface(), kSurface);
  ExpectInside("box point beyond +x face is outside", box.solid, box.OutsideX(), kOutside);
}

TEST(InsideClassification, Sphere) {
  const SphereFixture sphere("InsideSphere", 50.0 * mm);

  ExpectInside("sphere center is inside", sphere.solid, sphere.Center(), kInside);
  ExpectInside("sphere +x point is on the surface", sphere.solid, sphere.PositiveXSurface(),
               kSurface);
  ExpectInside("sphere point beyond radius is outside", sphere.solid, sphere.OutsideX(), kOutside);
}

TEST(InsideClassification, Cylinder) {
  const CylinderFixture cylinder("InsideCylinder", 25.0 * mm, 40.0 * mm);

  ExpectInside("cylinder center is inside", cylinder.solid, cylinder.Center(), kInside);
  ExpectInside("cylinder radial point is on the surface", cylinder.solid,
               cylinder.PositiveRadialSurface(), kSurface);
  ExpectInside("cylinder top point is on the surface", cylinder.solid, cylinder.PositiveZSurface(),
               kSurface);
  ExpectInside("cylinder point beyond radius is outside", cylinder.solid, cylinder.OutsideRadialX(),
               kOutside);
}

} // namespace
