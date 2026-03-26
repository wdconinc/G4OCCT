// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPAssembly_impl.hh
/// @brief Firewall bridge between the DD4hep plugin TU and G4OCCT/OCCT.
///
/// See G4OCCT_STEPSolid_impl.hh for a full explanation of the Printf
/// return-type collision that makes this firewall necessary.
///
/// This header must not pull in either DD4hep/ROOT or G4OCCT/OCC.

#ifndef G4OCCT_DD4HEP_STEPAssembly_impl_hh
#define G4OCCT_DD4HEP_STEPAssembly_impl_hh

#include <map>
#include <string>

class G4Material;

/// Import a STEP assembly, build the G4OCCTAssemblyVolume, and return the
/// number of constituent solids found.
/// @param path     Path to the STEP file.
/// @param materials  Map of STEP material name → G4Material*.
/// Throws @c std::runtime_error on failure.
int G4OCCT_ImportSTEPAssembly(const std::string&                    path,
                               const std::map<std::string, G4Material*>& materials);

#endif // G4OCCT_DD4HEP_STEPAssembly_impl_hh
