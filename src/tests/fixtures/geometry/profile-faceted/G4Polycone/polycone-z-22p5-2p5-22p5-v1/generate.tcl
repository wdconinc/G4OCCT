# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
pcone lower_segment 8 14 20
copytranslate lower_translated lower_segment 0 0 -22.5
pcone upper_segment 14 5 25
copytranslate upper_translated upper_segment 0 0 -2.5
bfuse seed_shape lower_translated upper_translated
stepwrite a seed_shape [file join $script_dir shape.step]
