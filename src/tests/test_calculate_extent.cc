// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4AffineTransform.hh>
#include <G4RotationMatrix.hh>
#include <G4SystemOfUnits.hh>
#include <G4VoxelLimits.hh>

#include <gtest/gtest.h>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::ExpectTrue;

constexpr G4double kExtentTolerance = 1.0e-6 * mm;

TEST(CalculateExtent, RotatedAndTranslatedExtent) {
  const BoxFixture box("CalculateExtentTransformBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  G4RotationMatrix rotation;
  rotation.rotateZ(90.0 * deg);
  const G4AffineTransform transform(rotation, G4ThreeVector(5.0 * mm, -7.0 * mm, 11.0 * mm));

  G4double min            = 0.0;
  G4double max            = 0.0;
  const G4bool intersects = box.solid.CalculateExtent(kXAxis, G4VoxelLimits(), transform, min, max);

  ExpectTrue("rotated box extent intersects world limits", intersects);
  ExpectNear("rotated box x-min follows transformed y half-length", min, -15.0 * mm,
             kExtentTolerance);
  ExpectNear("rotated box x-max follows transformed y half-length", max, 25.0 * mm,
             kExtentTolerance);
}

TEST(CalculateExtent, ExtentClipping) {
  const BoxFixture box("CalculateExtentClippedBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  G4VoxelLimits voxelLimits;
  voxelLimits.AddLimit(kXAxis, -4.0 * mm, 6.0 * mm);
  voxelLimits.AddLimit(kYAxis, -10.0 * mm, 10.0 * mm);

  G4double min = 0.0;
  G4double max = 0.0;
  const G4bool intersects =
      box.solid.CalculateExtent(kXAxis, voxelLimits, G4AffineTransform(), min, max);

  ExpectTrue("box extent intersects clipped voxel limits", intersects);
  ExpectNear("box x-min is clipped to voxel minimum", min, -4.0 * mm, kExtentTolerance);
  ExpectNear("box x-max is clipped to voxel maximum", max, 6.0 * mm, kExtentTolerance);
}

TEST(CalculateExtent, RejectsDisjointVoxel) {
  const BoxFixture box("CalculateExtentDisjointBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  G4VoxelLimits voxelLimits;
  voxelLimits.AddLimit(kYAxis, 25.0 * mm, 35.0 * mm);

  G4double min = 123.0 * mm;
  G4double max = 456.0 * mm;
  const G4bool intersects =
      box.solid.CalculateExtent(kXAxis, voxelLimits, G4AffineTransform(), min, max);

  ExpectTrue("box extent reports no intersection for disjoint orthogonal voxel", !intersects);
}

} // namespace
