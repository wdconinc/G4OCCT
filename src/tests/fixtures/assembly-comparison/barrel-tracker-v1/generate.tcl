# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# barrel-tracker-v1/generate.tcl
# Three concentric hollow silicon cylindrical tracking layers.
# Each layer: inner radius r mm, outer radius r+0.5 mm, height 500 mm.
# Layers are centred at z=0 by translating -250 mm along z.
#
# Run with:
#   DRAWEXE -b -f generate.tcl

pload ALL

set script_dir [file normalize [file dirname [info script]]]

XNewDoc D

foreach r {50 80 110} {
  set router [expr {$r + 0.5}]
  pcylinder outer_$r $router 500
  pcylinder inner_$r $r      500
  bcut shell_$r outer_$r inner_$r
  ttranslate shell_$r 0 0 -250
  set lbl [XAddShape D shell_$r]
  SetName D $lbl TrackingLayer
}

WriteStep D [file join $script_dir shape.step]
