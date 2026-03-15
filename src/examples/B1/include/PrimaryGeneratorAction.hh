// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file PrimaryGeneratorAction.hh
/// @brief Primary particle generator for the G4OCCT B1 example.

#ifndef B1_PrimaryGeneratorAction_hh
#define B1_PrimaryGeneratorAction_hh

#include <G4ParticleGun.hh>
#include <G4VUserPrimaryGeneratorAction.hh>

#include <memory>

/**
 * @brief Generates primary gamma rays for the B1 example.
 *
 * Fires a 6 MeV gamma along the +z axis from a random (x, y) position within
 * the envelope footprint at z = -envSizeZ/2 — identical to the standard
 * Geant4 basic/B1 setup.
 */
class PrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
 public:
  PrimaryGeneratorAction();
  ~PrimaryGeneratorAction() override = default;

  void GeneratePrimaries(G4Event* event) override;

  /// Read access to the underlying particle gun.
  const G4ParticleGun* GetParticleGun() const { return fParticleGun.get(); }

 private:
  std::unique_ptr<G4ParticleGun> fParticleGun;
};

#endif  // B1_PrimaryGeneratorAction_hh
