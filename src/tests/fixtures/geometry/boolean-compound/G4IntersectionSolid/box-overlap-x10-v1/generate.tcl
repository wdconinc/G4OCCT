# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

box left_box 20 20 20
box right_box -min 10 0 0 -size 20 20 20
bcommon intersection_shape left_box right_box
copytranslate final_shape intersection_shape -15 -10 -10
stepwrite a final_shape [file join $script_dir shape.step]
