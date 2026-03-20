// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4Polyhedron.hh>
#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <memory>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::ExpectTrue;

TEST(CreatePolyhedron, Box) {
  const BoxFixture box("PolyhedronBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const std::unique_ptr<G4Polyhedron> polyhedron(box.solid.CreatePolyhedron());

  ExpectTrue("CreatePolyhedron returns a polyhedron for a box", polyhedron != nullptr);
  ExpectTrue("box polyhedron has vertices",
             polyhedron != nullptr && polyhedron->GetNoVertices() > 0);
  ExpectTrue("box polyhedron has facets", polyhedron != nullptr && polyhedron->GetNoFacets() > 0);

  G4int vertexIndex = 0;
  G4int edgeFlag    = 0;
  ExpectTrue("box polyhedron exposes facet connectivity",
             polyhedron != nullptr && polyhedron->GetNextVertexIndex(vertexIndex, edgeFlag));
}

TEST(CreatePolyhedron, RepeatedCallsReturnConsistentResults) {
  const BoxFixture box("PolyhedronBoxCache", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const std::unique_ptr<G4Polyhedron> first(box.solid.CreatePolyhedron());
  const std::unique_ptr<G4Polyhedron> second(box.solid.CreatePolyhedron());

  ExpectTrue("first call returns a polyhedron", first != nullptr);
  ExpectTrue("second call returns a polyhedron", second != nullptr);
  ExpectTrue("both calls return distinct objects", first.get() != second.get());

  if (first && second) {
    ExpectTrue("vertex count is identical across repeated calls",
               first->GetNoVertices() == second->GetNoVertices());
    ExpectTrue("facet count is identical across repeated calls",
               first->GetNoFacets() == second->GetNoFacets());
  }
}

} // namespace
