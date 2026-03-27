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
///   OCCT declares:   Standard_EXPORT int Printf(const char* theFormat, ...);
///
/// Functions that differ only in return type cannot be overloaded.  The
/// firewall pattern keeps the two include worlds in separate TUs.  This header
/// is included in both the DD4hep-facing plugin TU and the OCCT impl TU; it
/// must not pull in either DD4hep/ROOT or G4OCCT/OCCT.

#ifndef G4OCCT_DD4HEP_STEPSolid_impl_hh
#define G4OCCT_DD4HEP_STEPSolid_impl_hh

#include <string>
#include <vector>

/// Plain vertex: three doubles — no OCCT or ROOT/DD4hep types.
struct G4OCCT_Vertex
{
  double x = 0., y = 0., z = 0.;
};

/// A single triangular facet: three vertices.
struct G4OCCT_Triangle
{
  G4OCCT_Vertex v[3];
};

/// Data returned by the OCCT impl TU to the DD4hep plugin TU.
/// Contains the full tessellated mesh so the DD4hep side can build a
/// TessellatedSolid instead of a bounding-box placeholder.
struct G4OCCT_STEPSolidGeometry
{
  std::vector<G4OCCT_Triangle> triangles; ///< Tessellated mesh triangles (mm).
};

/// Import a STEP file, tessellate it, and return the triangle mesh in mm.
/// Throws @c std::runtime_error on failure or empty mesh.
G4OCCT_STEPSolidGeometry G4OCCT_ImportSTEPSolid(const std::string& name,
                                                  const std::string& path);

#endif // G4OCCT_DD4HEP_STEPSolid_impl_hh
