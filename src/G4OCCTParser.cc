#include "G4OCCTParser.hh"

#include "G4OCCTSolid.hh"

#include <STEPControl_Reader.hxx>
#include <StepData_StepModel.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <IFSelect_ReturnStatus.hxx>

#include <cassert>

G4OCCTParser::G4OCCTParser(const G4String& filename)
{
  // TODO filename checking
  assert(filename.size() > 0);

  // read STEP file
  STEPControl_Reader reader; 
  IFSelect_ReturnStatus stat = reader.ReadFile(filename);

  // check STEP file
  bool failsonly = false;
  IFSelect_PrintCount mode = IFSelect_ItemsByEntity;
  reader.PrintCheckLoad(failsonly,mode);

  // load roots
  Standard_Integer nbr = reader.NbRootsForTransfer();
  for (Standard_Integer n = 1; n <= nbr; n++) {
    G4cout << "STEP: Transferring Root " << n << G4endl;
    reader.TransferRoot(n);
  }
  reader.PrintCheckTransfer(failsonly,mode);

  // load shapes
  Standard_Integer nbs = reader.NbShapes();
  for (Standard_Integer i = 1; i <= nbs; i++) {
    G4cout << "STEP:   Transferring Shape " << i << G4endl;
    TopoDS_Shape shape = reader.Shape(i);

    // load each solid as an own object
    TopExp_Explorer ex;
    for (ex.Init(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
      // get the shape
      const TopoDS_Solid& occt_solid = TopoDS::Solid(ex.Current());

      // create solid
      G4OCCTSolid solid(occt_solid);
    }

    // load all non-solids now
    for (ex.Init(shape, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next()) {
      // get the shape
      const TopoDS_Shell& shell = TopoDS::Shell(ex.Current());
    }
  }
}
