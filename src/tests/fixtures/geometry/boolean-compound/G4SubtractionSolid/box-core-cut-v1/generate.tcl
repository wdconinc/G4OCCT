# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

box outer_box 30 30 30
box core_box -min 10 10 10 -size 10 10 10
bcut subtraction_shape outer_box core_box
copytranslate final_shape subtraction_shape -15 -15 -15
stepwrite a final_shape [file join $script_dir shape.step]
