#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}

"${drawexe}" -b -f "${script_dir}/G4Box/box-20x30x40-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Cons/cons-r8-r3-z24-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4CutTubs/cut-tubs-r12-z40-tilted-x-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Orb/orb-r11-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Para/para-dx10-dy8-z20-alpha20-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Sphere/sphere-r15-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Torus/torus-rtor20-rmax5-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Trap/trap-dx7-13-dy9-z18-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Trd/trd-dx10-16-dy8-14-z20-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Tubs/tubs-r12-z35-v1/generate.tcl"
python3 "${script_dir}/../tools/normalize_step_header.py" \
  "${script_dir}/G4Box/box-20x30x40-v1/shape.step" \
  "${script_dir}/G4Cons/cons-r8-r3-z24-v1/shape.step" \
  "${script_dir}/G4CutTubs/cut-tubs-r12-z40-tilted-x-v1/shape.step" \
  "${script_dir}/G4Orb/orb-r11-v1/shape.step" \
  "${script_dir}/G4Para/para-dx10-dy8-z20-alpha20-v1/shape.step" \
  "${script_dir}/G4Sphere/sphere-r15-v1/shape.step" \
  "${script_dir}/G4Torus/torus-rtor20-rmax5-v1/shape.step" \
  "${script_dir}/G4Trap/trap-dx7-13-dy9-z18-v1/shape.step" \
  "${script_dir}/G4Trd/trd-dx10-16-dy8-14-z20-v1/shape.step" \
  "${script_dir}/G4Tubs/tubs-r12-z35-v1/shape.step"
