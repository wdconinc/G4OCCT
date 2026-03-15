# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
psphere base_sphere 10
scalexyz seed_shape base_sphere 1.5 1 2
stepwrite a seed_shape [file join $script_dir shape.step]
