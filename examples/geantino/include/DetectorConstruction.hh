#ifndef _DETECTORCONSTRUCTION_HH_
#define _DETECTORCONSTRUCTION_HH_

#include "G4VUserDetectorConstruction.hh"

class DetectorConstruction: public G4VUserDetectorConstruction
{
  public:
    DetectorConstruction(G4VPhysicalVolume* world = 0): fWorld(world) { }
    virtual G4VPhysicalVolume* Construct() { return fWorld; }

  private:
    G4VPhysicalVolume* fWorld{0};
};

#endif
