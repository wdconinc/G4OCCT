// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file generate_triple_box_step.cc
/// @brief Generate the triple-box STEP assembly fixture.
///
/// Creates a STEP AP214 file with three 10×10×10 mm boxes placed along X at
/// x = −12, 0, and +12 mm (leaving 2 mm gaps between adjacent boxes).  All
/// three parts carry the part name "Component" so that the assembly benchmark
/// can map them to G4_Al via a single G4OCCTMaterialMap entry.
///
/// The boxes are written as free shapes in an OCCT XDE document; STEP
/// AP214 part-name attributes survive the round-trip through
/// STEPCAFControl_Writer / STEPCAFControl_Reader and are used by
/// G4OCCTAssemblyVolume::FromSTEP as the material-map lookup key.
///
/// Usage:
///   generate_triple_box_step <output.step>

#include <BRepPrimAPI_MakeBox.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Pnt.hxx>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

/// X-coordinates of the box centres (mm).
constexpr std::array<double, 3> kBoxCentersX = {-12.0, 0.0, 12.0};

/// Half-size of each box (mm); box is 10×10×10 mm.
constexpr double kBoxHalfSize = 5.0;

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: generate_triple_box_step <output.step>\n";
    return EXIT_FAILURE;
  }
  const std::string output_path(argv[1]);

  // ── OCCT XDE document ────────────────────────────────────────────────────

  Handle(TDocStd_Application) app = new TDocStd_Application;
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-CAF", doc);

  Handle(XCAFDoc_ShapeTool) shapeTool  = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_MaterialTool) matTool = XCAFDoc_DocumentTool::MaterialTool(doc->Main());

  // ── Add three 10×10×10 mm boxes ─────────────────────────────────────────

  for (std::size_t i = 0; i < kBoxCentersX.size(); ++i) {
    const double cx = kBoxCentersX[i];
    // BRepPrimAPI_MakeBox takes the minimum-corner point and extents.
    const gp_Pnt corner(cx - kBoxHalfSize, -kBoxHalfSize, -kBoxHalfSize);
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(corner, 10.0, 10.0, 10.0).Shape();

    // Add as a free shape (not an assembly compound).
    TDF_Label shapeLabel = shapeTool->AddShape(box, /*makeAssembly=*/Standard_False);

    // Part name — used by G4OCCTAssemblyVolume as the material-map lookup key
    // when no explicit XDE material attribute is found after the STEP round-trip.
    TDataStd_Name::Set(shapeLabel, "Component");

    // XDE material attribute (density only; G4OCCTAssemblyVolume reads the name).
    TDF_Label matLabel = matTool->AddMaterial(
        new TCollection_HAsciiString("Component"), new TCollection_HAsciiString("G4_Al"), 2.699,
        new TCollection_HAsciiString("g/cm3"), new TCollection_HAsciiString("homogeneous"));
    matTool->SetMaterial(shapeLabel, matLabel);
  }

  // ── Write STEP ────────────────────────────────────────────────────────────

  STEPCAFControl_Writer writer;
  writer.SetNameMode(Standard_True);
  writer.SetMatMode(Standard_True);
  if (writer.Transfer(doc) != Standard_True) {
    std::cerr << "STEP transfer failed for " << output_path << '\n';
    return EXIT_FAILURE;
  }
  if (writer.Write(output_path.c_str()) != IFSelect_RetDone) {
    std::cerr << "STEP write failed for " << output_path << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "Generated: " << output_path << '\n';
  return EXIT_SUCCESS;
}
