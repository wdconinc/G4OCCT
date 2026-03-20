# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline profile 0 0 -20 8 0 -20 12 0 -5 7 0 5 11 0 0 9 0 15 5 0 20 0 0 20 0 0 -20
revol seed_shape profile 0 0 0 0 0 1 360
stepwrite a seed_shape [file join $script_dir shape.step]
