// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#ifndef G4OCCT_TESTS_NAVIGATION_TEST_HARNESS_HH
#define G4OCCT_TESTS_NAVIGATION_TEST_HARNESS_HH

#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <string>

namespace g4occt::tests::navigation {

inline constexpr G4double kDefaultTolerance = 1e-9 * mm;

inline std::string ToString(const G4ThreeVector& vector) {
  std::ostringstream buffer;
  buffer << "(" << vector.x() << ", " << vector.y() << ", " << vector.z() << ")";
  return buffer.str();
}

inline std::string ToString(const EInside classification) {
  switch (classification) {
  case kOutside:
    return "kOutside";
  case kSurface:
    return "kSurface";
  case kInside:
    return "kInside";
  }

  return "<unknown EInside>";
}

inline void ExpectTrue(const std::string& label, const bool condition) {
  EXPECT_TRUE(condition) << label;
}

inline void ExpectNear(const std::string& label, const G4double actual, const G4double expected,
                       const G4double tolerance = kDefaultTolerance) {
  if (std::isinf(expected)) {
    EXPECT_TRUE(std::isinf(actual)) << label << ": expected infinity, got " << actual;
    return;
  }
  EXPECT_NEAR(actual, expected, tolerance) << label;
}

inline void ExpectVectorNear(const std::string& label, const G4ThreeVector& actual,
                             const G4ThreeVector& expected,
                             const G4double tolerance = kDefaultTolerance) {
  EXPECT_LE((actual - expected).mag(), tolerance)
      << label << ": expected " << ToString(expected) << ", got " << ToString(actual);
}

inline void ExpectInsideClassification(const std::string& label, const EInside actual,
                                       const EInside expected) {
  EXPECT_EQ(actual, expected) << label << ": expected " << ToString(expected) << ", got "
                              << ToString(actual);
}

inline void ExpectInside(const std::string& label, const G4OCCTSolid& solid,
                         const G4ThreeVector& point, const EInside expected) {
  ExpectInsideClassification(label, solid.Inside(point), expected);
}

inline void ExpectSurfaceNormal(const std::string& label, const G4OCCTSolid& solid,
                                const G4ThreeVector& point, const G4ThreeVector& expected,
                                const G4double tolerance = kDefaultTolerance) {
  ExpectVectorNear(label, solid.SurfaceNormal(point), expected, tolerance);
}

inline void ExpectDistanceToIn(const std::string& label, const G4OCCTSolid& solid,
                               const G4ThreeVector& point, const G4double expected,
                               const G4double tolerance = kDefaultTolerance) {
  ExpectNear(label, solid.DistanceToIn(point), expected, tolerance);
}

inline void ExpectDistanceToIn(const std::string& label, const G4OCCTSolid& solid,
                               const G4ThreeVector& point, const G4ThreeVector& direction,
                               const G4double expected,
                               const G4double tolerance = kDefaultTolerance) {
  ExpectNear(label, solid.DistanceToIn(point, direction), expected, tolerance);
}

inline void ExpectDistanceToOut(const std::string& label, const G4OCCTSolid& solid,
                                const G4ThreeVector& point, const G4double expected,
                                const G4double tolerance = kDefaultTolerance) {
  ExpectNear(label, solid.DistanceToOut(point), expected, tolerance);
}

inline void ExpectDistanceToOut(const std::string& label, const G4OCCTSolid& solid,
                                const G4ThreeVector& point, const G4ThreeVector& direction,
                                const G4double expected,
                                const G4double tolerance = kDefaultTolerance) {
  ExpectNear(label, solid.DistanceToOut(point, direction), expected, tolerance);
}

inline void ExpectDistanceToOutWithNormal(const std::string& label, const G4OCCTSolid& solid,
                                          const G4ThreeVector& point,
                                          const G4ThreeVector& direction,
                                          const G4double expectedDistance,
                                          const G4ThreeVector& expectedNormal,
                                          const G4double tolerance = kDefaultTolerance) {
  G4bool validNormal = false;
  G4ThreeVector normal;
  const G4double distance = solid.DistanceToOut(point, direction, true, &validNormal, &normal);

  ExpectNear(label + " distance", distance, expectedDistance, tolerance);
  ExpectTrue(label + " valid normal", validNormal);
  ExpectVectorNear(label + " normal", normal, expectedNormal, tolerance);
}

struct BoxFixture {
  G4double halfX;
  G4double halfY;
  G4double halfZ;
  TopoDS_Shape shape;
  G4OCCTSolid solid;

  explicit BoxFixture(const G4String& name, const G4double inHalfX = 10.0 * mm,
                      const G4double inHalfY = 20.0 * mm, const G4double inHalfZ = 30.0 * mm)
      : halfX(inHalfX)
      , halfY(inHalfY)
      , halfZ(inHalfZ)
      , shape(BRepPrimAPI_MakeBox(gp_Pnt(-halfX, -halfY, -halfZ), gp_Pnt(halfX, halfY, halfZ))
                  .Shape())
      , solid(name, shape) {}

  G4ThreeVector Center() const { return {0.0, 0.0, 0.0}; }
  G4ThreeVector PositiveXSurface() const { return {halfX, 0.0, 0.0}; }
  G4ThreeVector PositiveYSurface() const { return {0.0, halfY, 0.0}; }
  G4ThreeVector PositiveZSurface() const { return {0.0, 0.0, halfZ}; }
  G4ThreeVector OutsideX(const G4double margin = 5.0 * mm) const {
    return {halfX + margin, 0.0, 0.0};
  }
};

struct SphereFixture {
  G4double radius;
  TopoDS_Shape shape;
  G4OCCTSolid solid;

  explicit SphereFixture(const G4String& name, const G4double inRadius = 50.0 * mm)
      : radius(inRadius)
      , shape(BRepPrimAPI_MakeSphere(gp_Pnt(0.0, 0.0, 0.0), radius).Shape())
      , solid(name, shape) {}

  G4ThreeVector Center() const { return {0.0, 0.0, 0.0}; }
  G4ThreeVector PositiveXSurface() const { return {radius, 0.0, 0.0}; }
  G4ThreeVector OutsideX(const G4double margin = 5.0 * mm) const {
    return {radius + margin, 0.0, 0.0};
  }
};

struct CylinderFixture {
  G4double radius;
  G4double halfLength;
  TopoDS_Shape shape;
  G4OCCTSolid solid;

  explicit CylinderFixture(const G4String& name, const G4double inRadius = 25.0 * mm,
                           const G4double inHalfLength = 50.0 * mm)
      : radius(inRadius)
      , halfLength(inHalfLength)
      , shape(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.0, 0.0, -halfLength), gp_Dir(0.0, 0.0, 1.0)),
                                       radius, 2.0 * halfLength)
                  .Shape())
      , solid(name, shape) {}

  G4ThreeVector Center() const { return {0.0, 0.0, 0.0}; }
  G4ThreeVector PositiveRadialSurface() const { return {radius, 0.0, 0.0}; }
  G4ThreeVector PositiveZSurface() const { return {0.0, 0.0, halfLength}; }
  G4ThreeVector OutsideRadialX(const G4double margin = 5.0 * mm) const {
    return {radius + margin, 0.0, 0.0};
  }
};

} // namespace g4occt::tests::navigation

#endif // G4OCCT_TESTS_NAVIGATION_TEST_HARNESS_HH
