#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}

"${drawexe}" -b -f "${script_dir}/G4EllipticalTube/elliptical-tube-12x7x25-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Ellipsoid/ellipsoid-15x10x20-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4EllipticalCone/elliptical-cone-z30-cut15-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4GenericPolycone/generic-polycone-nonmonotonic-z-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4GenericTrap/generic-trap-skewed-z20-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Hype/hype-r8-stereo20-z25-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Paraboloid/paraboloid-r6-r18-z20-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4Polycone/polycone-z-22p5-2p5-22p5-v1/generate.tcl"
python3 "${script_dir}/G4Polyhedra/polyhedra-hex-r10-r6-z25-v1/generate.py"
"${drawexe}" -b -f "${script_dir}/G4Tet/tet-right-20x30x40-v1/generate.tcl"
python3 "${script_dir}/../tools/normalize_step_header.py" \
  "${script_dir}/G4Ellipsoid/ellipsoid-15x10x20-v1/shape.step" \
  "${script_dir}/G4EllipticalCone/elliptical-cone-z30-cut15-v1/shape.step" \
  "${script_dir}/G4EllipticalTube/elliptical-tube-12x7x25-v1/shape.step" \
  "${script_dir}/G4GenericPolycone/generic-polycone-nonmonotonic-z-v1/shape.step" \
  "${script_dir}/G4GenericTrap/generic-trap-skewed-z20-v1/shape.step" \
  "${script_dir}/G4Hype/hype-r8-stereo20-z25-v1/shape.step" \
  "${script_dir}/G4Paraboloid/paraboloid-r6-r18-z20-v1/shape.step" \
  "${script_dir}/G4Polycone/polycone-z-22p5-2p5-22p5-v1/shape.step" \
  "${script_dir}/G4Polyhedra/polyhedra-hex-r10-r6-z25-v1/shape.step" \
  "${script_dir}/G4Tet/tet-right-20x30x40-v1/shape.step"
