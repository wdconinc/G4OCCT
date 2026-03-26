// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPSolid_impl.hh
/// @brief Firewall bridge between the DD4hep plugin TU and G4OCCT/OCCT.
///
/// Including DD4hep headers (which transitively include ROOT's TString.h) in
/// the same translation unit as G4OCCT headers (which transitively include
/// opencascade/Standard_CString.hxx) produces a hard error:
///
///   ROOT declares:   extern void Printf(const char *fmt, ...);
///   OCC declares:    Standard_EXPORT int Printf(const char* theFormat, ...);
///
/// Functions that differ only in return type cannot be overloaded.  The
/// firewall pattern keeps the two include worlds in separate TUs.  This header
/// is included in both the DD4hep-facing plugin TU and the OCC impl TU; it
/// must not pull in either DD4hep/ROOT or G4OCCT/OCC.

#ifndef G4OCCT_DD4HEP_STEPSolid_impl_hh
#define G4OCCT_DD4HEP_STEPSolid_impl_hh

#include <string>

/// Plain data returned by the OCC impl TU to the DD4hep plugin TU.
struct G4OCCT_STEPSolidGeometry
{
  double halfX = 0.;
  double halfY = 0.;
  double halfZ = 0.;
};

/// Import a STEP file and return its axis-aligned bounding half-extents (mm).
/// Throws @c std::runtime_error on failure or degenerate bounding box.
G4OCCT_STEPSolidGeometry G4OCCT_ImportSTEPSolid(const std::string& name,
                                                  const std::string& path);

#endif // G4OCCT_DD4HEP_STEPSolid_impl_hh
