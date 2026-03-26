// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTActionInitialization.cc
/// @brief Minimal user action initialisation for the g4occt executable.

#include "G4OCCTActionInitialization.hh"

#include <G4ParticleGun.hh>
#include <G4ParticleTable.hh>
#include <G4SystemOfUnits.hh>
#include <G4VUserPrimaryGeneratorAction.hh>

namespace {

/// Default primary generator: one 1 GeV proton per event along +Z.
class DefaultPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
public:
  DefaultPrimaryGeneratorAction()
      : fGun(std::make_unique<G4ParticleGun>(1)) {
    auto* proton = G4ParticleTable::GetParticleTable()->FindParticle("proton");
    fGun->SetParticleDefinition(proton);
    fGun->SetParticleEnergy(1.0 * GeV);
    fGun->SetParticleMomentumDirection(G4ThreeVector(0, 0, 1));
    fGun->SetParticlePosition(G4ThreeVector(0, 0, 0));
  }

  void GeneratePrimaries(G4Event* event) override { fGun->GeneratePrimaryVertex(event); }

private:
  std::unique_ptr<G4ParticleGun> fGun;
};

} // namespace

void G4OCCTActionInitialization::Build() const {
  SetUserAction(new DefaultPrimaryGeneratorAction);
}
