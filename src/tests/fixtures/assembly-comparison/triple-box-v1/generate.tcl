# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

# Generate the triple-box STEP assembly for the assembly-comparison benchmark.
#
# Three 10×10×10 mm boxes centred at x = -12, 0, and +12 mm (2 mm gaps).
# Each box is added to an XDE document as a free shape named "Component" so
# that G4OCCTAssemblyVolume::FromSTEP can map all three to the same G4Material
# via a single G4OCCTMaterialMap entry.
#
# Run with:
#   DRAWEXE -b -f generate.tcl

pload ALL

set script_dir [file normalize [file dirname [info script]]]

# Create three 10×10×10 mm boxes (min-corner form: -min x y z -size dx dy dz).
box box1 -min -17 -5 -5 -size 10 10 10
box box2 -min  -5 -5 -5 -size 10 10 10
box box3 -min   7 -5 -5 -size 10 10 10

# Build an XDE document with three named free shapes.
XNewDoc D
XAddShape D box1
XAddShape D box2
XAddShape D box3
XSetName D 0:1:1 Component
XSetName D 0:1:2 Component
XSetName D 0:1:3 Component

# Export to STEP with XDE attributes (names written as PRODUCT records).
WriteStep D [file join $script_dir shape.step]
