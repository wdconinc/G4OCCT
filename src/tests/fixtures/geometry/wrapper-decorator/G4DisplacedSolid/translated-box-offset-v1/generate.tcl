# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

# G4DisplacedSolid(base, nullptr, translation) places the *center* of base at
# the translation vector.  G4Box(halfX, halfY, halfZ) is centered at the
# origin, so the displaced solid spans:
#   [tx-halfX, tx+halfX] x [ty-halfY, ty+halfY] x [tz-halfZ, tz+halfZ]
# In DRAWEXE, `box` creates a box with one CORNER at the origin, so we must
# translate by (tx-halfX, ty-halfY, tz-halfZ) = (12-10, -7-15, 25-20) = (2,-22,5)
# to align the center of the STEP solid with the Geant4 displaced solid center.
box displaced_box 20 30 40
ttranslate displaced_box 2 -22 5
stepwrite a displaced_box [file join $script_dir shape.step]
