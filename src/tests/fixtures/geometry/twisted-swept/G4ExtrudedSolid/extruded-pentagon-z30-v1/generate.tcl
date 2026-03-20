# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline base_wire 0 0 -15 30 0 -15 30 10 -15 10 20 -15 0 20 -15 0 0 -15
mkplane base_face base_wire
prism extruded_shape base_face 0 0 30
stepwrite a extruded_shape [file join $script_dir shape.step]
