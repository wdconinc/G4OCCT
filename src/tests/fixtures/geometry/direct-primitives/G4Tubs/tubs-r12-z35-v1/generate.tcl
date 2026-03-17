# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
set tools_dir [file normalize [file join $script_dir ../../../tools]]

pcylinder seed_tubs 12 35
stepwrite a seed_tubs [file join $script_dir shape.step]
exec python3 [file join $tools_dir translate_step.py] [file join $script_dir shape.step] 0 0 -17.5
