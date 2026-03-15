# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

pcylinder raw_tube 12 40
translate raw_tube 0 0 -20
polyline slab_bottom -50 -50 -30 50 -50 -10 50 50 -10 -50 50 -30 -50 -50 -30
polyline slab_top -50 -50 15 50 -50 25 50 50 25 -50 50 15 -50 -50 15
thrusections cut_slab 1 1 slab_bottom slab_top -safe
bcommon seed_shape raw_tube cut_slab
stepwrite a seed_shape [file join $script_dir shape.step]
