# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
# cmake-format: on

cmake_minimum_required(VERSION 3.16)

# ─── bench_navigator ─────────────────────────────────────────────────────────
add_executable(bench_navigator bench_navigator.cc)
target_link_libraries(bench_navigator PRIVATE G4OCCT)

# Benchmarks are not added to CTest by default; run manually with:
# ./bench_navigator [N_geantinos]
