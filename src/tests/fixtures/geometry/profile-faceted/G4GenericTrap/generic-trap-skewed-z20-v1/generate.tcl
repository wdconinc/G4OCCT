# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
polyline bottom_face -12 -8 -20 10 -8 -20 14 9 -20 -9 7 -20 -12 -8 -20
polyline top_face -8 -6 20 13 -7 20 9 10 20 -11 8 20 -8 -6 20
thrusections seed_shape 1 1 bottom_face top_face -safe
stepwrite a seed_shape [file join $script_dir shape.step]
