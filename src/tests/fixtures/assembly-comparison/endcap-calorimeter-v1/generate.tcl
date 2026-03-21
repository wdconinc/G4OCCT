# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# endcap-calorimeter-v1/generate.tcl
# 5×5 grid of 40×40×200 mm calorimeter tower modules.
# Module centres at x,y ∈ {−120,−60,0,60,120} mm, z = 0–200 mm.
#
# Run with:
#   DRAWEXE -b -f generate.tcl

pload ALL
XNewDoc D

set script_dir [file normalize [file dirname [info script]]]

foreach xi {-120 -60 0 60 120} {
  foreach yi {-120 -60 0 60 120} {
    set x [expr {$xi - 20}]
    set y [expr {$yi - 20}]
    set name mod_${xi}_${yi}
    box $name $x $y 0 40 40 200
    set lbl [XAddShape D $name]
    SetName D $lbl CaloModule
  }
}

WriteStep D [file join $script_dir shape.step]
