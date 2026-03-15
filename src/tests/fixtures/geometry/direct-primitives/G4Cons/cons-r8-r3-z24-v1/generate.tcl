# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

pcone seed_cons 8 3 24
stepwrite a seed_cons [file join $script_dir shape.step]
