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

#include <cmath>
#include <cstdlib>
#include <iostream>
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

[[noreturn]] inline void Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << "\n";
  std::exit(EXIT_FAILURE);
}

inline void Pass(const std::string& message) { std::cout << "PASS: " << message << "\n"; }

inline void ExpectTrue(const std::string& message, const bool condition) {
  if (!condition) {
    Fail(message);
  }

  Pass(message);
}

inline void ExpectNear(const std::string& message, const G4double actual, const G4double expected,
                       const G4double tolerance = kDefaultTolerance) {
  if (std::isinf(expected)) {
    if (!std::isinf(actual)) {
      Fail(message + ": expected infinity, got " + std::to_string(actual));
    }

    Pass(message);
    return;
  }

  if (std::fabs(actual - expected) > tolerance) {
    std::ostringstream buffer;
    buffer << message << ": expected " << expected << " ± " << tolerance << ", got " << actual;
    Fail(buffer.str());
  }

  Pass(message);
}

inline void ExpectVectorNear(const std::string& message, const G4ThreeVector& actual,
                             const G4ThreeVector& expected,
                             const G4double tolerance = kDefaultTolerance) {
  if ((actual - expected).mag() > tolerance) {
    Fail(message + ": expected " + ToString(expected) + ", got " + ToString(actual));
  }

  Pass(message);
}

inline void ExpectInsideClassification(const std::string& message, const EInside actual,
                                       const EInside expected) {
  if (actual != expected) {
    Fail(message + ": expected " + ToString(expected) + ", got " + ToString(actual));
  }

  Pass(message);
}

inline void ExpectInside(const std::string& message, const G4OCCTSolid& solid,
                         const G4ThreeVector& point, const EInside expected) {
  ExpectInsideClassification(message, solid.Inside(point), expected);
}

inline void ExpectSurfaceNormal(const std::string& message, const G4OCCTSolid& solid,
                                const G4ThreeVector& point, const G4ThreeVector& expected,
                                const G4double tolerance = kDefaultTolerance) {
  ExpectVectorNear(message, solid.SurfaceNormal(point), expected, tolerance);
}

inline void ExpectDistanceToIn(const std::string& message, const G4OCCTSolid& solid,
                               const G4ThreeVector& point, const G4double expected,
                               const G4double tolerance = kDefaultTolerance) {
  ExpectNear(message, solid.DistanceToIn(point), expected, tolerance);
}

inline void ExpectDistanceToIn(const std::string& message, const G4OCCTSolid& solid,
                               const G4ThreeVector& point, const G4ThreeVector& direction,
                               const G4double expected,
                               const G4double tolerance = kDefaultTolerance) {
  ExpectNear(message, solid.DistanceToIn(point, direction), expected, tolerance);
}

inline void ExpectDistanceToOut(const std::string& message, const G4OCCTSolid& solid,
                                const G4ThreeVector& point, const G4double expected,
                                const G4double tolerance = kDefaultTolerance) {
  ExpectNear(message, solid.DistanceToOut(point), expected, tolerance);
}

inline void ExpectDistanceToOut(const std::string& message, const G4OCCTSolid& solid,
                                const G4ThreeVector& point, const G4ThreeVector& direction,
                                const G4double expected,
                                const G4double tolerance = kDefaultTolerance) {
  ExpectNear(message, solid.DistanceToOut(point, direction), expected, tolerance);
}

inline void ExpectDistanceToOutWithNormal(const std::string& message, const G4OCCTSolid& solid,
                                          const G4ThreeVector& point,
                                          const G4ThreeVector& direction,
                                          const G4double expectedDistance,
                                          const G4ThreeVector& expectedNormal,
                                          const G4double tolerance = kDefaultTolerance) {
  G4bool validNormal = false;
  G4ThreeVector normal;
  const G4double distance = solid.DistanceToOut(point, direction, true, &validNormal, &normal);

  ExpectNear(message + " distance", distance, expectedDistance, tolerance);
  ExpectTrue(message + " valid normal", validNormal);
  ExpectVectorNear(message + " normal", normal, expectedNormal, tolerance);
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
