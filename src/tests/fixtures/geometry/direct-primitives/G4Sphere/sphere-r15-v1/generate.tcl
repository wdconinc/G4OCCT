# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

psphere seed_sphere 15
stepwrite a seed_sphere [file join $script_dir shape.step]
