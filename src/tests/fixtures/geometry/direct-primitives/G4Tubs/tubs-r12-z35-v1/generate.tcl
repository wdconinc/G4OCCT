# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

pcylinder seed_tubs 12 35
translate seed_tubs 0 0 -17.5
stepwrite a seed_tubs [file join $script_dir shape.step]
