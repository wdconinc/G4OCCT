#include "G4OCCT/G4OCCTPlacement.hh"

G4OCCTPlacement::G4OCCTPlacement(G4RotationMatrix* pRot,
                                  const G4ThreeVector& tlate,
                                  G4LogicalVolume* pCurrentLogical,
                                  const G4String& pName,
                                  G4LogicalVolume* pMotherLogical,
                                  G4bool pMany,
                                  G4int pCopyNo,
                                  const TopLoc_Location& location,
                                  G4bool pSurfChk)
    : G4PVPlacement(pRot, tlate, pCurrentLogical, pName,
                    pMotherLogical, pMany, pCopyNo, pSurfChk),
      fLocation(location) {}
