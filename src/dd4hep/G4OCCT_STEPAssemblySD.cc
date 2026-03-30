// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPAssemblySD.cc
/// @brief Implementation of G4OCCTAssemblySDSetup.

#include "G4OCCT_STEPAssemblySD.hh"

#include "G4OCCT/G4OCCTAssemblyRegistry.hh"
#include "G4OCCT/G4OCCTAssemblyVolume.hh"
#include "G4OCCT/G4OCCTSensitiveDetectorMap.hh"

#include <G4Exception.hh>
#include <G4String.hh>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

std::size_t G4OCCTAssemblySDSetup::Apply(
    const std::string& detectorName,
    const std::vector<std::pair<std::string, G4VSensitiveDetector*>>& assignments) {
  G4OCCTAssemblyVolume* assembly = G4OCCTAssemblyRegistry::Instance().Get(detectorName);
  if (!assembly) {
    G4Exception("G4OCCTAssemblySDSetup::Apply", "G4OCCT_ASDSD000", JustWarning,
                ("Assembly '" + detectorName +
                 "' not found in G4OCCTAssemblyRegistry. "
                 "Ensure G4OCCT_STEPAssembly was used to import this detector.")
                    .c_str());
    return 0;
  }

  G4OCCTSensitiveDetectorMap sdMap;
  for (const auto& [pattern, sd] : assignments) {
    sdMap.Add(G4String(pattern), sd);
  }
  return assembly->ApplySDMap(sdMap);
}
