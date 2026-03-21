# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# string-array-v1/generate.tcl
# 3×3 strings × 5 optical modules = 45 cylindrical modules.
# Inspired by neutrino telescope string arrays (IceCube-like geometry).
# Module: cylinder R=60 mm, H=120 mm.
# String centres: sx,sy ∈ {−500,0,500} mm (500 mm pitch).
# Module z centres: −400,−200,0,200,400 mm (200 mm pitch).
#
# Run with:
#   DRAWEXE -b -f generate.tcl

pload ALL
XNewDoc D

set script_dir [file normalize [file dirname [info script]]]

foreach sx {-500 0 500} {
  foreach sy {-500 0 500} {
    foreach sz {-400 -200 0 200 400} {
      set name mod_${sx}_${sy}_${sz}
      pcylinder $name 60 120
      ttranslate $name $sx $sy [expr {$sz - 60}]
      set lbl [XAddShape D $name]
      SetName D $lbl OpticalModule
    }
  }
}

WriteStep D [file join $script_dir shape.step]
