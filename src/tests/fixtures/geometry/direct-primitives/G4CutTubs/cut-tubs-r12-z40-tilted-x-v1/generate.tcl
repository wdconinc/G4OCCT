# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
set tools_dir [file normalize [file join $script_dir ../../../tools]]

# DRAWEXE's translate command does not persist a TopLoc_Location through
# bcommon, so the cylinder is constructed at z=0 and the slab planes are
# shifted up by 25 mm so that bcommon cuts both ends correctly.  After the
# boolean, translate_step.py shifts the resulting STEP geometry down by 25 mm
# into the correct Geant4 reference frame (z = -22.4 .. +21.2 at the cylinder
# edge).
pcylinder raw_tube 12 60
polyline slab_bottom -50 -50 -5 50 -50 15 50 50 15 -50 50 -5 -50 -50 -5
polyline slab_top    -50 -50 40 50 -50 50 50 50 50 -50 50 40 -50 -50 40
thrusections cut_slab 1 1 slab_bottom slab_top -safe
bcommon seed_shape raw_tube cut_slab
stepwrite a seed_shape [file join $script_dir shape.step]
exec python3 [file join $tools_dir translate_step.py] [file join $script_dir shape.step] 0 0 -25
