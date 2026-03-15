# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

box left_box -min -6 0 0 -size 10 10 10
box middle_box -min 0 0 0 -size 10 10 10
box right_box -min 6 0 0 -size 10 10 10
bfuse first_pair left_box middle_box
bfuse multi_union_shape first_pair right_box
stepwrite a multi_union_shape [file join $script_dir shape.step]
