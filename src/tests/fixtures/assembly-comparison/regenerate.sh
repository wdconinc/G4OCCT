#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

# Regenerate assembly-comparison STEP fixtures.
#
# Preferred: DRAWEXE (generate.tcl) — produces output identical to what
# G4OCCTAssemblyVolume::FromSTEP reads back via STEPCAFControl_Reader.
# Fallback: generate.py — pure-Python STEP generator, no external tools needed.
#
# Usage:
#   bash src/tests/fixtures/assembly-comparison/regenerate.sh
#   DRAWEXE=/path/to/DRAWEXE bash src/tests/fixtures/assembly-comparison/regenerate.sh

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}

run_fixture() {
  local fixture_dir="$1"

  if command -v "$drawexe" >/dev/null 2>&1; then
    echo "Using DRAWEXE: $drawexe"
    "$drawexe" -b -f "${fixture_dir}/generate.tcl"
  else
    echo "DRAWEXE not found at '$drawexe'; falling back to generate.py"
    python3 "${fixture_dir}/generate.py"
  fi
}

run_fixture "${script_dir}/triple-box-v1"

normalizer="${script_dir}/../geometry/tools/normalize_step_header.py"
if [ ! -f "$normalizer" ]; then
  echo "Warning: normalize_step_header.py not found at $normalizer; skipping header normalisation"
else
  python3 "$normalizer" "${script_dir}/triple-box-v1/shape.step"
fi
