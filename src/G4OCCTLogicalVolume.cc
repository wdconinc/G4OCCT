#include "G4OCCT/G4OCCTLogicalVolume.hh"

G4OCCTLogicalVolume::G4OCCTLogicalVolume(G4VSolid* pSolid,
                                         G4Material* pMaterial,
                                         const G4String& name,
                                         const TopoDS_Shape& shape)
    : G4LogicalVolume(pSolid, pMaterial, name), fShape(shape) {}
