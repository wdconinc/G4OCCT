// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTLogicalVolume.cc
/// @brief Implementation of G4OCCTLogicalVolume.

#include "G4OCCT/G4OCCTLogicalVolume.hh"

G4OCCTLogicalVolume::G4OCCTLogicalVolume(G4VSolid* pSolid,
                                         G4Material* pMaterial,
                                         const G4String& name,
                                         const TopoDS_Shape& shape)
    : G4LogicalVolume(pSolid, pMaterial, name), fShape(shape) {}
