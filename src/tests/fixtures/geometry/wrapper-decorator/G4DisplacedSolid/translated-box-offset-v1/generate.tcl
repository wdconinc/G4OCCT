# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

box displaced_box 20 30 40
ttranslate displaced_box 12 -7 25
stepwrite a displaced_box [file join $script_dir shape.step]
