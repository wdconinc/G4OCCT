#include "PrimaryGeneratorAction.hh"

#include "G4Event.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4ParticleDefinition.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"

PrimaryGeneratorAction::PrimaryGeneratorAction()
: G4VUserPrimaryGeneratorAction()
{
  fParticleGun.SetParticleDefinition(
               G4ParticleTable::GetParticleTable()->FindParticle("geantino"));
  fParticleGun.SetParticleEnergy(1.0*GeV);
  fParticleGun.SetParticlePosition(G4ThreeVector{0,0,0});
}

void PrimaryGeneratorAction::GeneratePrimaries(G4Event* anEvent)
{
  double th = std::acos(G4RandFlat::shoot(-1.0, 1.0));
  double ph = G4RandFlat::shoot(0.0, 360*deg);
  G4ThreeVector p(sin(th)*cos(ph), sin(th)*sin(ph), cos(th));
  fParticleGun.SetParticleMomentumDirection(p);
  fParticleGun.GeneratePrimaryVertex(anEvent);
}
