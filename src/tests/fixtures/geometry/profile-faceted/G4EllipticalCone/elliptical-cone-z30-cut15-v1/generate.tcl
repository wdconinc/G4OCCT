# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
pcone base_cone 24 8 30
scalexyz scaled_cone base_cone 1 0.625 1
copytranslate final_cone scaled_cone 0 0 -15
stepwrite a final_cone [file join $script_dir shape.step]
