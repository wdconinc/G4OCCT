// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTAssemblyRegistry.cc
/// @brief Implementation of G4OCCTAssemblyRegistry.

#include "G4OCCT/G4OCCTAssemblyRegistry.hh"

#include "G4OCCT/G4OCCTAssemblyVolume.hh"

#include <stdexcept>

G4OCCTAssemblyRegistry& G4OCCTAssemblyRegistry::Instance() {
  static G4OCCTAssemblyRegistry instance;
  return instance;
}

G4OCCTAssemblyRegistry::~G4OCCTAssemblyRegistry() {
  for (auto& [name, assembly] : fAssemblies) {
    delete assembly;
  }
}

void G4OCCTAssemblyRegistry::Register(const std::string& name, G4OCCTAssemblyVolume* assembly) {
  if (fAssemblies.count(name) != 0) {
    throw std::runtime_error("G4OCCTAssemblyRegistry: assembly '" + name
                             + "' is already registered.");
  }
  fAssemblies[name] = assembly;
}

G4OCCTAssemblyVolume* G4OCCTAssemblyRegistry::Get(const std::string& name) const {
  const auto it = fAssemblies.find(name);
  return it != fAssemblies.end() ? it->second : nullptr;
}

G4OCCTAssemblyVolume* G4OCCTAssemblyRegistry::Release(const std::string& name) {
  const auto it = fAssemblies.find(name);
  if (it == fAssemblies.end()) {
    return nullptr;
  }
  G4OCCTAssemblyVolume* assembly = it->second;
  fAssemblies.erase(it);
  return assembly;
}

std::size_t G4OCCTAssemblyRegistry::Size() const {
  return fAssemblies.size();
}
