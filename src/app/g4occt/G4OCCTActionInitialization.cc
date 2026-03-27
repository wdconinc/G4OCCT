// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTActionInitialization.cc
/// @brief User action initialisation for the g4occt executable.

#include "G4OCCTActionInitialization.hh"
#include "G4OCCTEventAction.hh"
#include "G4OCCTRunAction.hh"
#include "G4OCCTSteppingAction.hh"
#include "G4OCCTTrackingAction.hh"

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

void G4OCCTActionInitialization::BuildForMaster() const {
  SetUserAction(new G4OCCTRunAction(&fConfig));
}

void G4OCCTActionInitialization::Build() const {
  SetUserAction(new DefaultPrimaryGeneratorAction);

  auto* runAction = new G4OCCTRunAction(&fConfig);
  SetUserAction(runAction);

  auto* eventAction = new G4OCCTEventAction(runAction);
  SetUserAction(eventAction);

  auto* trackingAction = new G4OCCTTrackingAction(eventAction, runAction);
  SetUserAction(trackingAction);

  SetUserAction(new G4OCCTSteppingAction(eventAction, trackingAction, runAction));
}

