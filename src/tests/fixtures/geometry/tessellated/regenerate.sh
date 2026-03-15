#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}

"${drawexe}" -b -f "${script_dir}/G4TessellatedSolid/tessellated-tet-right-20x30x40-v1/generate.tcl"
python3 "${script_dir}/../tools/normalize_step_header.py" \
  "${script_dir}/G4TessellatedSolid/tessellated-tet-right-20x30x40-v1/shape.step"
