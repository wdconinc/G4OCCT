#include "G4OCCTSolid.hh"

#include <G4VSolid.hh>

#include <TopoDS_Solid.hxx>
#include <BRepExtrema_DistShapeShape.hxx>

G4OCCTSolid::G4OCCTSolid(const TopoDS_Solid& solid)
: G4VSolid("name")
{
  G4cout << "Creating solid" << G4endl;
}

