#ifndef _PRIMARYGENERATORACTION_HH_
#define _PRIMARYGENERATORACTION_HH_

#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4ParticleGun.hh"

class PrimaryGeneratorAction: public G4VUserPrimaryGeneratorAction
{
  public:
    PrimaryGeneratorAction();
    virtual ~PrimaryGeneratorAction() = default;

    virtual void GeneratePrimaries(G4Event* anEvent);

  private:
    G4ParticleGun fParticleGun{1};
};

#endif
