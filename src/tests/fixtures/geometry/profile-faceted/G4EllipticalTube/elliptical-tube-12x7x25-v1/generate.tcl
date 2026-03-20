# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

pcylinder seed_cylinder 12 50
scalexyz scaled_shape seed_cylinder 1 0.5833333333333334 1
copytranslate seed_shape scaled_shape 0 0 -25
stepwrite a seed_shape [file join $script_dir shape.step]
