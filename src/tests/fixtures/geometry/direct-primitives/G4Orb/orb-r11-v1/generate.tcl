# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

psphere seed_orb 11
stepwrite a seed_orb [file join $script_dir shape.step]
