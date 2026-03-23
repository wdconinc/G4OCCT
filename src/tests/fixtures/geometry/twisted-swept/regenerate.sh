#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out_dir=$(mktemp -d)
trap 'rm -rf "$out_dir"' EXIT

cxx=${CXX:-c++}
"${cxx}" -std=c++17 \
  "${script_dir}/tools/generate_twisted_fixtures.cc" \
  -I /opt/local/include/opencascade \
  -L /opt/local/lib \
  -lTKDESTEP -lTKXSBase -lTKOffset -lTKTopAlgo -lTKBRep -lTKPrim -lTKGeomAlgo -lTKG3d -lTKG2d -lTKGeomBase -lTKMath -lTKernel \
  -o "${out_dir}/generate_twisted_fixtures"

run_fixture() {
  local fixture_name=$1
  local target=$2
  LD_LIBRARY_PATH=/opt/local/lib:${LD_LIBRARY_PATH:-} \
    "${out_dir}/generate_twisted_fixtures" "${fixture_name}" "${target}"
}

run_fixture twisted-box "${script_dir}/G4TwistedBox/box-dx10-dy8-z20-phi30-v1/shape.step"
run_fixture twisted-trd "${script_dir}/G4TwistedTrd/trd-dx10-16-dy8-14-z20-phi30-v1/shape.step"
run_fixture twisted-trap "${script_dir}/G4TwistedTrap/trap-dx7-13-dy9-z18-phi30-v1/shape.step"
run_fixture twisted-tubs "${script_dir}/G4TwistedTubs/tubs-r6-r12-z20-dphi120-phi30-v1/shape.step"

drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}
"${drawexe}" -b -f \
  "${script_dir}/G4ExtrudedSolid/extruded-pentagon-z30-v1/generate.tcl"

python3 "${script_dir}/../tools/normalize_step_header.py" \
  "${script_dir}/G4TwistedBox/box-dx10-dy8-z20-phi30-v1/shape.step" \
  "${script_dir}/G4TwistedTrd/trd-dx10-16-dy8-14-z20-phi30-v1/shape.step" \
  "${script_dir}/G4TwistedTrap/trap-dx7-13-dy9-z18-phi30-v1/shape.step" \
  "${script_dir}/G4TwistedTubs/tubs-r6-r12-z20-dphi120-phi30-v1/shape.step" \
  "${script_dir}/G4ExtrudedSolid/extruded-pentagon-z30-v1/shape.step"
