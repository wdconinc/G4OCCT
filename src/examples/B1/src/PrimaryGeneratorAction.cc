// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file PrimaryGeneratorAction.cc
/// @brief Implementation of the B1 primary generator.

#include "PrimaryGeneratorAction.hh"

#include <G4Event.hh>
#include <G4ParticleDefinition.hh>
#include <G4ParticleTable.hh>
#include <G4SystemOfUnits.hh>
#include <Randomize.hh>

PrimaryGeneratorAction::PrimaryGeneratorAction() {
  fParticleGun = std::make_unique<G4ParticleGun>(1);

  G4ParticleTable* particleTable = G4ParticleTable::GetParticleTable();
  G4ParticleDefinition* particle = particleTable->FindParticle("gamma");

  fParticleGun->SetParticleDefinition(particle);
  fParticleGun->SetParticleMomentumDirection(G4ThreeVector(0.0, 0.0, 1.0));
  fParticleGun->SetParticleEnergy(6.0 * MeV);
}

void PrimaryGeneratorAction::GeneratePrimaries(G4Event* event) {
  // Shoot from a random (x, y) within the ±10 cm envelope footprint,
  // always from z = -15 cm (the upstream face of the envelope).
  const G4double envHalfXY = 10.0 * cm;
  G4double x0              = envHalfXY * (2.0 * G4UniformRand() - 1.0);
  G4double y0              = envHalfXY * (2.0 * G4UniformRand() - 1.0);

  fParticleGun->SetParticlePosition(G4ThreeVector(x0, y0, -15.0 * cm));
  fParticleGun->GeneratePrimaryVertex(event);
}
