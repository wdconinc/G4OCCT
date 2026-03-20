# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

box seed_box -10 -15 -20 20 30 40
stepwrite a seed_box [file join $script_dir shape.step]
