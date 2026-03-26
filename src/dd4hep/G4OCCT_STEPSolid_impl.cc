// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCT_STEPSolid_impl.cc
/// @brief OCC-side implementation of the STEP solid import bridge.
///
/// This TU includes G4OCCT and OpenCASCADE headers and must NOT include any
/// DD4hep or ROOT headers to avoid the Printf return-type collision (see
/// G4OCCT_STEPSolid_impl.hh).

#include "G4OCCT_STEPSolid_impl.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <G4ThreeVector.hh>

#include <stdexcept>
#include <string>

G4OCCT_STEPSolidGeometry G4OCCT_ImportSTEPSolid(const std::string& name,
                                                  const std::string& path)
{
  G4OCCTSolid* solid = nullptr;
  try {
    solid = G4OCCTSolid::FromSTEP(name, path);
  } catch (const std::exception& ex) {
    throw std::runtime_error("G4OCCT_STEPSolid: failed to import '" + path +
                             "' (" + ex.what() + ")");
  }

  G4ThreeVector pMin, pMax;
  solid->BoundingLimits(pMin, pMax);

  const double halfX = (pMax.x() - pMin.x()) / 2.0;
  const double halfY = (pMax.y() - pMin.y()) / 2.0;
  const double halfZ = (pMax.z() - pMin.z()) / 2.0;

  if (halfX <= 0.0 || halfY <= 0.0 || halfZ <= 0.0) {
    throw std::runtime_error("G4OCCT_STEPSolid: bounding box of '" + path +
                             "' has zero or negative extent");
  }

  return {halfX, halfY, halfZ};
}
