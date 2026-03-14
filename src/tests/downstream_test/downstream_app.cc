// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// downstream_app.cc
// Minimal application that exercises the G4OCCT public API to verify that
// the installed headers and library are correctly found by find_package.

#include <G4OCCT/G4OCCTSolid.hh>
#include <G4OCCT/G4OCCTLogicalVolume.hh>
#include <G4OCCT/G4OCCTPlacement.hh>

#include <BRepPrimAPI_MakeBox.hxx>

#include <iostream>

int main() {
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  G4OCCTSolid solid("DownstreamBox", box);
  std::cout << "G4OCCTSolid entity type: " << solid.GetEntityType() << "\n";
  std::cout << "Downstream find_package(G4OCCT) test: PASSED\n";
  return 0;
}
