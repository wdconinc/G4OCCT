//
// ********************************************************************
// * License and Disclaimer                                           *
// *                                                                  *
// * The  Geant4 software  is  copyright of the Copyright Holders  of *
// * the Geant4 Collaboration.  It is provided  under  the terms  and *
// * conditions of the Geant4 Software License,  included in the file *
// * LICENSE and available at  http://cern.ch/geant4/license .  These *
// * include a list of copyright holders.                             *
// *                                                                  *
// * Neither the authors of this software system, nor their employing *
// * institutes,nor the agencies providing financial support for this *
// * work  make  any representation or  warranty, express or implied, *
// * regarding  this  software system or assume any liability for its *
// * use.  Please see the license in the file  LICENSE  and URL above *
// * for the full disclaimer and the limitation of liability.         *
// *                                                                  *
// * This  code  implementation is the result of  the  scientific and *
// * technical work of the GEANT4 collaboration.                      *
// * By using,  copying,  modifying or  distributing the software (or *
// * any work based  on the software)  you  agree  to acknowledge its *
// * use  in  resulting  scientific  publications,  and indicate your *
// * acceptance of all terms of the Geant4 Software license.          *
// ********************************************************************
//
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
