// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <iostream>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::ExpectTrue;
using g4occt::tests::navigation::ExpectVectorNear;

constexpr G4double kTolerance = 1.0e-6 * mm;

void TestDistanceToOutWithoutNormalRequest() {
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

void TestDistanceToOutWithNormal() {
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

void TestDistanceToOutWithMissingNormalBuffer() {
  const BoxFixture box("DistanceToOutMissingNormalBuffer");

  G4bool validNormal = true;

  const G4double distance = box.solid.DistanceToOut(box.Center(), G4ThreeVector(0.0, 0.0, 1.0),
                                                    true, &validNormal, nullptr);

  ExpectNear("DistanceToOut still returns distance when normal buffer is null", distance, box.halfZ,
             kTolerance);
  ExpectTrue("DistanceToOut keeps validNorm false without a normal buffer", !validNormal);
}

void TestDistanceToOutWithNullNormalOutputs() {
  const BoxFixture box("DistanceToOutNullOutputs");

  const G4double distance =
      box.solid.DistanceToOut(box.Center(), G4ThreeVector(0.0, 1.0, 0.0), true, nullptr, nullptr);

  ExpectNear("DistanceToOut works when normal outputs are omitted", distance, box.halfY,
             kTolerance);
}

} // namespace

int main() {
  TestDistanceToOutWithoutNormalRequest();
  TestDistanceToOutWithNormal();
  TestDistanceToOutWithMissingNormalBuffer();
  TestDistanceToOutWithNullNormalOutputs();

  std::cout << "\nAll test_distance_to_out_ray tests passed.\n";
  return 0;
}
