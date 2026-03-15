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
