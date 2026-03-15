// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4Polyhedron.hh>
#include <G4SystemOfUnits.hh>

#include <iostream>
#include <memory>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::ExpectTrue;

void TestBoxCreatePolyhedron() {
  const BoxFixture box("PolyhedronBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const std::unique_ptr<G4Polyhedron> polyhedron(box.solid.CreatePolyhedron());

  ExpectTrue("CreatePolyhedron returns a polyhedron for a box", polyhedron != nullptr);
  ExpectTrue("box polyhedron has vertices",
             polyhedron != nullptr && polyhedron->GetNoVertices() > 0);
  ExpectTrue("box polyhedron has facets",
             polyhedron != nullptr && polyhedron->GetNoFacets() > 0);

  G4int vertexIndex = 0;
  G4int edgeFlag = 0;
  ExpectTrue("box polyhedron exposes facet connectivity",
             polyhedron != nullptr && polyhedron->GetNextVertexIndex(vertexIndex, edgeFlag));
}

}  // namespace

int main() {
  TestBoxCreatePolyhedron();

  std::cout << "\nAll test_create_polyhedron tests passed.\n";
  return 0;
}
