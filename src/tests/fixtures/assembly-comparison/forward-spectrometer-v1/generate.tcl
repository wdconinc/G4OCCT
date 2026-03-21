# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# forward-spectrometer-v1/generate.tcl
# Fixed-target forward spectrometer:
#   8 silicon tracking planes (300×300×1 mm) at z = 0, 200, 400, 600,
#   1400, 1600, 1800, 2000 mm;
#   iron dipole magnet (500×300×600 mm outer, 100×100×600 mm gap) at
#   z = 700–1300 mm;
#   lead ECAL block (400×400×300 mm) at z = 2100–2400 mm.
#
# Run with:
#   DRAWEXE -b -f generate.tcl

pload ALL
XNewDoc D

set script_dir [file normalize [file dirname [info script]]]

# Upstream silicon tracking planes
foreach z {0 200 400 600} {
  box tp_$z -150 -150 $z 300 300 1
  set lbl [XAddShape D tp_$z]
  SetName D $lbl TrackingPlane
}

# Iron dipole magnet: outer yoke minus central gap
box yoke_outer -250 -150 700 500 300 600
box yoke_gap    -50  -50 700 100 100 600
bcut dipole yoke_outer yoke_gap
set lbl [XAddShape D dipole]
SetName D $lbl DipoleMagnet

# Downstream silicon tracking planes
foreach z {1400 1600 1800 2000} {
  box tp_$z -150 -150 $z 300 300 1
  set lbl [XAddShape D tp_$z]
  SetName D $lbl TrackingPlane
}

# Lead ECAL block
box ecal -200 -200 2100 400 400 300
set lbl [XAddShape D ecal]
SetName D $lbl ECALBlock

WriteStep D [file join $script_dir shape.step]
