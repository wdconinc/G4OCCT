#ifndef _G4OCCTPARSER_HH_
#define _G4OCCTPARSER_HH_

#include "G4VPhysicalVolume.hh"
#include "G4SystemOfUnits.hh"

class G4OCCTParser
{
  public:
    G4OCCTParser(const G4String& filename = "");
    virtual ~G4OCCTParser() = default;

  public:
    G4VPhysicalVolume* GetWorldVolume() const { return fWorld; };
  private:
    G4VPhysicalVolume* fWorld{nullptr};
};

#endif
