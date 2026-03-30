// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPAssembly_impl.cc
/// @brief OCCT-side implementation of the STEP assembly import bridge.
///
/// This TU includes G4OCCT and OpenCASCADE headers and must NOT include any
/// DD4hep or ROOT headers to avoid the Printf return-type collision (see
/// G4OCCT_STEPAssembly_impl.hh).

#include "G4OCCT_STEPAssembly_impl.hh"

#include "G4OCCT/G4OCCTAssemblyRegistry.hh"
#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTMaterialMap.hh"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

class G4Material;

int G4OCCT_ImportSTEPAssembly(const std::string& path,
                               const std::map<std::string, G4Material*>& materials,
                               const std::string& detectorName,
                               const std::vector<std::string>& /*sensitiveNames*/) {
  G4OCCTMaterialMap matMap;
  for (const auto& [stepName, g4mat] : materials) {
    matMap.Add(stepName, g4mat);
  }

  G4OCCTAssemblyVolume* assembly = nullptr;
  try {
    assembly = G4OCCTAssemblyVolume::FromSTEP(path, matMap);
  } catch (const std::exception& ex) {
    throw std::runtime_error("G4OCCT_STEPAssembly: failed to import '" + path + "' (" + ex.what() +
                             ")");
  }

  const int nConstituents = static_cast<int>(assembly->GetLogicalVolumes().size());
  G4OCCTAssemblyRegistry::Instance().Register(detectorName, assembly);
  assembly = nullptr; // registry owns it now
  return nConstituents;
}
