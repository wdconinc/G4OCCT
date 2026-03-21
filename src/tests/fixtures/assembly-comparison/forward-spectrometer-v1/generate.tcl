# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# forward-spectrometer-v1/generate.tcl
# Fixed-target forward spectrometer with geometry centred at z = 0 (world frame).
#
# The GDML world is a 700×700×2500 mm box centred at the origin, so it spans
# z = −1250 to +1250 mm.  All shapes below are created at positions relative
# to this world centre (absolute detector z minus 1250 mm):
#
#   8 silicon tracking planes (300×300×1 mm) low-face at z = −1250, −1050, −850, −650,
#                                                              150,   350,  550,  750 mm
#                                          (centres at z + 0.5 mm);
#   iron dipole magnet (500×300×600 mm outer, 100×100×600 mm gap) at
#                                                     z = −550 to +50 mm;
#   lead ECAL block (400×400×300 mm) at z = 850–1150 mm.
#
# Run with:
#   DRAWEXE -b -f generate.tcl

pload ALL
XNewDoc D

set script_dir [file normalize [file dirname [info script]]]

# Upstream silicon tracking planes (detector z = 0,200,400,600 → world z = −1250,−1050,−850,−650)
foreach z {-1250 -1050 -850 -650} {
  box tp_$z -150 -150 $z 300 300 1
  set lbl [XAddShape D tp_$z]
  SetName D $lbl TrackingPlane
}

# Iron dipole magnet: outer yoke minus central gap
# Detector z = 700–1300 mm → world z = −550 to +50 mm
box yoke_outer -250 -150 -550 500 300 600
box yoke_gap    -50  -50 -550 100 100 600
bcut dipole yoke_outer yoke_gap
set lbl [XAddShape D dipole]
SetName D $lbl DipoleMagnet

# Downstream silicon tracking planes (detector z = 1400,1600,1800,2000 → world z = 150,350,550,750)
foreach z {150 350 550 750} {
  box tp_$z -150 -150 $z 300 300 1
  set lbl [XAddShape D tp_$z]
  SetName D $lbl TrackingPlane
}

# Lead ECAL block (detector z = 2100–2400 mm → world z = 850–1150 mm)
box ecal -200 -200 850 400 400 300
set lbl [XAddShape D ecal]
SetName D $lbl ECALBlock

WriteStep D [file join $script_dir shape.step]
