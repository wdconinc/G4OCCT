// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTMaterialMap.cc
/// @brief Implementation of G4OCCTMaterialMap.

#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <G4Exception.hh>
#include <G4Material.hh>

void G4OCCTMaterialMap::Add(const G4String& stepName, G4Material* material) {
  fMap[stepName] = material;
}

G4Material* G4OCCTMaterialMap::Resolve(const G4String& stepName) const {
  auto it = fMap.find(stepName);
  if (it == fMap.end()) {
    G4Exception("G4OCCTMaterialMap::Resolve", "G4OCCT_MatMap001", FatalException,
                ("Unresolved STEP material name: \"" + stepName +
                 "\". Register it with G4OCCTMaterialMap::Add() before importing.")
                    .c_str());
    return nullptr; // unreachable; silences compiler warnings
  }
  return it->second;
}

bool G4OCCTMaterialMap::Contains(const G4String& stepName) const {
  return fMap.count(stepName) > 0;
}
