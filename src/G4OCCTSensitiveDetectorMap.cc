// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSensitiveDetectorMap.cc
/// @brief Implementation of G4OCCTSensitiveDetectorMap.

#include "G4OCCT/G4OCCTSensitiveDetectorMap.hh"

#include <G4Exception.hh>
#include <G4VSensitiveDetector.hh>

#include <algorithm>
#include <cctype>

void G4OCCTSensitiveDetectorMap::Add(const G4String& pattern, G4VSensitiveDetector* sd) {
  if (!sd) {
    G4Exception("G4OCCTSensitiveDetectorMap::Add", "G4OCCT_SDMap000", FatalException,
                ("Null G4VSensitiveDetector passed for pattern '" + pattern + "'.").c_str());
    return;
  }
  for (auto& entry : fEntries) {
    if (entry.first == pattern) {
      entry.second = sd;
      return;
    }
  }
  fEntries.emplace_back(pattern, sd);
}

G4VSensitiveDetector* G4OCCTSensitiveDetectorMap::Resolve(const G4String& volumeName) const {
  for (const auto& entry : fEntries) {
    const G4String& pattern = entry.first;

    // Exact match
    if (volumeName == pattern) {
      return entry.second;
    }

    // Prefix match: volumeName starts with pattern+"_" and suffix is all digits
    const G4String prefix = pattern + "_";
    if (volumeName.rfind(prefix, 0) == 0) {
      const G4String suffix = volumeName.substr(prefix.size());
      if (!suffix.empty() &&
          std::all_of(suffix.begin(), suffix.end(),
                      [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        return entry.second;
      }
    }
  }
  return nullptr;
}
