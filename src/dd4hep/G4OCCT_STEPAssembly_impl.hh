// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPAssembly_impl.hh
/// @brief Firewall bridge between the DD4hep plugin TU and G4OCCT/OCCT.
///
/// See G4OCCT_STEPSolid_impl.hh for a full explanation of the Printf
/// return-type collision that makes this firewall necessary.
///
/// This header must not pull in either DD4hep/ROOT or G4OCCT/OCCT.

#ifndef G4OCCT_DD4HEP_STEPAssembly_impl_hh
#define G4OCCT_DD4HEP_STEPAssembly_impl_hh

#include <map>
#include <string>
#include <vector>

class G4Material;

/// Import a STEP assembly, build the G4OCCTAssemblyVolume, register it in
/// G4OCCTAssemblyRegistry under @p detectorName, and return the number of
/// constituent solids found.  The assembly lifetime is managed by the registry.
/// @param path             Path to the STEP file.
/// @param materials        Map of STEP material name → G4Material*.
/// @param detectorName     Key to register the assembly under in G4OCCTAssemblyRegistry.
/// @param sensitiveNames   Volume name patterns to receive SD assignment (informational;
///                          SD assignment happens at ConstructSDandField time).
/// Throws std::runtime_error on failure.
int G4OCCT_ImportSTEPAssembly(const std::string& path,
                              const std::map<std::string, G4Material*>& materials,
                              const std::string& detectorName,
                              const std::vector<std::string>& sensitiveNames);

#endif // G4OCCT_DD4HEP_STEPAssembly_impl_hh
