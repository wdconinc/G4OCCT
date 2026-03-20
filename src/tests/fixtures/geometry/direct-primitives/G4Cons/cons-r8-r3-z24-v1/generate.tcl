# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
set tools_dir [file normalize [file join $script_dir ../../../tools]]

pcone seed_cons 8 3 24
stepwrite a seed_cons [file join $script_dir shape.step]
exec python3 [file join $tools_dir translate_step.py] [file join $script_dir shape.step] 0 0 -12
