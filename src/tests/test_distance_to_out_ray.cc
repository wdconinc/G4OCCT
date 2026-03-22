// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <cmath>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectDistanceToOutWithNormal;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::ExpectTrue;
using g4occt::tests::navigation::ExpectVectorNear;
using g4occt::tests::navigation::SphereFixture;

constexpr G4double kTolerance = 1.0e-6 * mm;

TEST(DistanceToOutRay, WithoutNormalRequest) {
  const BoxFixture box("DistanceToOutNoNormal");

  G4bool validNormal = true;
  G4ThreeVector normal(-7.0 * mm, 2.0 * mm, 5.0 * mm);
  const G4ThreeVector originalNormal = normal;

  const G4double distance = box.solid.DistanceToOut(box.Center(), G4ThreeVector(1.0, 0.0, 0.0),
                                                    false, &validNormal, &normal);

  ExpectNear("DistanceToOut without calcNorm returns +x exit distance", distance, box.halfX,
             kTolerance);
  ExpectTrue("DistanceToOut without calcNorm leaves validNorm false", !validNormal);
  ExpectVectorNear("DistanceToOut without calcNorm leaves normal untouched", normal, originalNormal,
                   kTolerance);
}

TEST(DistanceToOutRay, WithNormal) {
  const BoxFixture box("DistanceToOutWithNormal");

  G4bool validNormal = false;
  G4ThreeVector normal;

  const G4double distance = box.solid.DistanceToOut(box.Center(), G4ThreeVector(1.0, 0.0, 0.0),
                                                    true, &validNormal, &normal);

  ExpectNear("DistanceToOut with calcNorm returns +x exit distance", distance, box.halfX,
             kTolerance);
  ExpectTrue("DistanceToOut with calcNorm marks valid normal", validNormal);
  ExpectVectorNear("DistanceToOut with calcNorm returns +x outward normal", normal,
                   G4ThreeVector(1.0, 0.0, 0.0), kTolerance);
}

TEST(DistanceToOutRay, WithMissingNormalBuffer) {
  const BoxFixture box("DistanceToOutMissingNormalBuffer");

  G4bool validNormal = true;

  const G4double distance = box.solid.DistanceToOut(box.Center(), G4ThreeVector(0.0, 0.0, 1.0),
                                                    true, &validNormal, nullptr);

  ExpectNear("DistanceToOut still returns distance when normal buffer is null", distance, box.halfZ,
             kTolerance);
  ExpectTrue("DistanceToOut keeps validNorm false without a normal buffer", !validNormal);
}

TEST(DistanceToOutRay, WithNullNormalOutputs) {
  const BoxFixture box("DistanceToOutNullOutputs");

  const G4double distance =
      box.solid.DistanceToOut(box.Center(), G4ThreeVector(0.0, 1.0, 0.0), true, nullptr, nullptr);

  ExpectNear("DistanceToOut works when normal outputs are omitted", distance, box.halfY,
             kTolerance);
}

// Verify that the cached-adaptor normal path returns the correct outward normal
// for each face of a box — exercising all six face entries in fFaceBoundsCache.
TEST(DistanceToOutRay, BoxNormalsAllSixFaces) {
  const BoxFixture box("DistanceToOutBoxNormals", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  ExpectDistanceToOutWithNormal("box +x face", box.solid, box.Center(),
                                G4ThreeVector(1.0, 0.0, 0.0), box.halfX,
                                G4ThreeVector(1.0, 0.0, 0.0), kTolerance);
  ExpectDistanceToOutWithNormal("box -x face", box.solid, box.Center(),
                                G4ThreeVector(-1.0, 0.0, 0.0), box.halfX,
                                G4ThreeVector(-1.0, 0.0, 0.0), kTolerance);
  ExpectDistanceToOutWithNormal("box +y face", box.solid, box.Center(),
                                G4ThreeVector(0.0, 1.0, 0.0), box.halfY,
                                G4ThreeVector(0.0, 1.0, 0.0), kTolerance);
  ExpectDistanceToOutWithNormal("box -y face", box.solid, box.Center(),
                                G4ThreeVector(0.0, -1.0, 0.0), box.halfY,
                                G4ThreeVector(0.0, -1.0, 0.0), kTolerance);
  ExpectDistanceToOutWithNormal("box +z face", box.solid, box.Center(),
                                G4ThreeVector(0.0, 0.0, 1.0), box.halfZ,
                                G4ThreeVector(0.0, 0.0, 1.0), kTolerance);
  ExpectDistanceToOutWithNormal("box -z face", box.solid, box.Center(),
                                G4ThreeVector(0.0, 0.0, -1.0), box.halfZ,
                                G4ThreeVector(0.0, 0.0, -1.0), kTolerance);
}

// Verify normal retrieval for a sphere (non-planar face).
TEST(DistanceToOutRay, SphereNormal) {
  const SphereFixture sphere("DistanceToOutSphereNormal", 50.0 * mm);

  // Ray along +x from centre exits at the +x pole; outward normal is (1,0,0).
  ExpectDistanceToOutWithNormal("sphere +x normal", sphere.solid, sphere.Center(),
                                G4ThreeVector(1.0, 0.0, 0.0), sphere.radius,
                                G4ThreeVector(1.0, 0.0, 0.0), kTolerance);

  // Ray along diagonal exits at the diagonal pole; normal equals the exit direction.
  const G4double kInvSqrt3 = 1.0 / std::sqrt(3.0);
  const G4ThreeVector diagonalDir(kInvSqrt3, kInvSqrt3, kInvSqrt3);
  ExpectDistanceToOutWithNormal("sphere diagonal normal", sphere.solid, sphere.Center(),
                                diagonalDir, sphere.radius, diagonalDir, kTolerance);
}

// Verify normal retrieval for a cylinder — both the curved lateral face and the
// flat end-cap face.
TEST(DistanceToOutRay, CylinderNormals) {
  const CylinderFixture cylinder("DistanceToOutCylinderNormal", 25.0 * mm, 40.0 * mm);

  // Radial ray along +x exits on the lateral face; outward normal is (1,0,0).
  ExpectDistanceToOutWithNormal("cylinder radial normal", cylinder.solid, cylinder.Center(),
                                G4ThreeVector(1.0, 0.0, 0.0), cylinder.radius,
                                G4ThreeVector(1.0, 0.0, 0.0), kTolerance);

  // Axial ray along +z exits on the top flat face; outward normal is (0,0,1).
  ExpectDistanceToOutWithNormal("cylinder top-cap normal", cylinder.solid, cylinder.Center(),
                                G4ThreeVector(0.0, 0.0, 1.0), cylinder.halfLength,
                                G4ThreeVector(0.0, 0.0, 1.0), kTolerance);
}

} // namespace
