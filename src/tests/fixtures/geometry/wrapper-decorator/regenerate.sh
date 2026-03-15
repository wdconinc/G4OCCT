#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}

"${drawexe}" -b -f "${script_dir}/G4DisplacedSolid/translated-box-offset-v1/generate.tcl"
"${drawexe}" -b -f "${script_dir}/G4ScaledSolid/scaled-sphere-nonuniform-v1/generate.tcl"

python3 - <<'PY' \
  "${script_dir}/G4DisplacedSolid/translated-box-offset-v1/shape.step" \
  "${script_dir}/G4ScaledSolid/scaled-sphere-nonuniform-v1/shape.step"
import re
import sys
from pathlib import Path

fixed_timestamp = '2024-01-01T00:00:00'
pattern = re.compile(r"(FILE_NAME\('[^']*',')([^']*)(')")
for raw_path in sys.argv[1:]:
    path = Path(raw_path)
    text = path.read_text(encoding='utf-8')
    normalized, count = pattern.subn(
        lambda match: f"{match.group(1)}{fixed_timestamp}{match.group(3)}",
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError(f'Could not normalize FILE_NAME timestamp in {path}')
    path.write_text(normalized, encoding='utf-8')
PY
