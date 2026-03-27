# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

# Coverage instrumentation and report target using bilke/cmake-modules
# CodeCoverage.
#
# This module is included by the top-level CMakeLists.txt when USE_COVERAGE=ON.
# It configures the compiler-appropriate gcov/llvm-cov tool at CMake configure
# time (eliminating the need for fragile shell-side CMakeCache.txt parsing),
# then defines a `coverage-report` build target that runs ctest and produces an
# HTML report plus a JSON summary via gcovr.
#
# Usage (from the build directory): cmake --build . --target coverage-report
#
# gcovr must be on PATH when the target is invoked.  In CI a venv is used:
# python3 -m venv .venv-gcovr && .venv-gcovr/bin/pip install gcovr
# PATH="$(pwd)/.venv-gcovr/bin:$PATH" cmake --build build --target
# coverage-report

include(FetchContent)

# Pin to a specific commit for reproducibility; bilke/cmake-modules has no tags.
FetchContent_Declare(
  cmake_modules
  GIT_REPOSITORY https://github.com/bilke/cmake-modules.git
  GIT_TAG 5b988b5beb64270cf68b7d6c20298ebc8236b580)
FetchContent_MakeAvailable(cmake_modules)
list(APPEND CMAKE_MODULE_PATH "${cmake_modules_SOURCE_DIR}")

# ── Detect coverage tool matching the active compiler ────────────────────────
# Use private _g4occt_* variables for find_program to avoid polluting the cache
# with an intermediate NOTFOUND value that would cause CodeCoverage.cmake's own
# find_program(GCOV_PATH NAMES gcov) to re-run and hit its FATAL_ERROR. After
# detection, GCOV_PATH is set with FORCE so CodeCoverage's check passes.
get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Prefer llvm-cov (native Clang coverage tool).  Search in the compiler's
  # directory first (spack installations co-locate all LLVM binaries), then fall
  # through to the default PATH search.
  find_program(
    _g4occt_llvm_cov
    NAMES llvm-cov
    HINTS "${_cxx_dir}")
  if(_g4occt_llvm_cov)
    set(_gcov_executable "${_g4occt_llvm_cov} gcov")
    set(GCOV_PATH
        "${_g4occt_llvm_cov}"
        CACHE FILEPATH "llvm-cov command for Clang coverage" FORCE)
    message(
      STATUS "Coverage: Clang + llvm-cov; gcov executable: ${_gcov_executable}")
  else()
    # llvm-cov not found.  Clang's --coverage flag generates GCOV-compatible
    # .gcno/.gcda files that can also be processed by system gcov.
    find_program(
      _g4occt_gcov
      NAMES gcov gcov-14 gcov-13
      HINTS "${_cxx_dir}")
    if(_g4occt_gcov)
      set(_gcov_executable "${_g4occt_gcov}")
    else()
      set(_g4occt_gcov "gcov")
      set(_gcov_executable "gcov")
    endif()
    set(GCOV_PATH
        "${_g4occt_gcov}"
        CACHE FILEPATH "gcov command (llvm-cov fallback for Clang coverage)"
              FORCE)
    message(STATUS "Coverage: Clang + system gcov (llvm-cov not found); "
                   "gcov executable: ${_gcov_executable}")
  endif()
else()
  # GCC: find gcov from the same toolchain directory to avoid version mismatches
  # with any system-installed /usr/bin/gcov-* binaries.
  find_program(
    _g4occt_gcov
    NAMES gcov gcov-14 gcov-13
    HINTS "${_cxx_dir}")
  if(_g4occt_gcov)
    set(_gcov_executable "${_g4occt_gcov}")
  else()
    set(_g4occt_gcov "gcov")
    set(_gcov_executable "gcov")
  endif()
  set(GCOV_PATH
      "${_g4occt_gcov}"
      CACHE FILEPATH "gcov command for GCC coverage" FORCE)
  message(STATUS "Coverage: GCC; gcov executable: ${_gcov_executable}")
endif()

# gcovr is installed into a venv at CI build time and need not exist at
# configure time.  Pre-set the CACHE variable so that CodeCoverage.cmake's
# `if(NOT GCOVR_PATH) → FATAL_ERROR` does not fire during configuration.
if(NOT GCOVR_PATH)
  set(GCOVR_PATH
      "gcovr"
      CACHE FILEPATH
            "gcovr command; must be on PATH when coverage-report is built")
endif()

include(CodeCoverage)

# Add --coverage, -g, -fprofile-abs-path, and (when supported)
# -fprofile-update=atomic to all targets via CMAKE_CXX_FLAGS / CMAKE_C_FLAGS.
append_coverage_compiler_flags()

# Supplement with flags that improve line-level accuracy but are not part of the
# upstream CodeCoverage module's default set.
add_compile_options(-fno-inline -fno-omit-frame-pointer)

# coverage-report target: runs ctest (excluding dd4hep plugin tests, which run
# in their own CI job) and generates an HTML report + JSON summary via gcovr.
include(ProcessorCount)
ProcessorCount(_nproc)
if(_nproc EQUAL 0)
  set(_nproc 1)
endif()

# setup_target_for_coverage_gcovr_html() stops the custom-target pipeline when
# ctest fails, which prevents gcovr from running and generating reports for
# failing test runs.  Instead, write a small CMake -P script that: 1. runs ctest
# and records its exit code (does not stop on failure), 2. runs gcovr
# unconditionally to produce HTML + JSON reports, 3. propagates any failure via
# FATAL_ERROR after the report is generated.
set(_coverage_runner "${CMAKE_BINARY_DIR}/coverage-runner.cmake")
file(
  WRITE "${_coverage_runner}"
  "cmake_minimum_required(VERSION 3.16)\n"
  "# Step 1: run ctest, capture exit code without aborting.\n"
  "execute_process(\n"
  "  COMMAND \"${CMAKE_CTEST_COMMAND}\" --test-dir \"${CMAKE_BINARY_DIR}\"\n"
  "          --output-on-failure -LE dd4hep -j ${_nproc}\n"
  "  RESULT_VARIABLE _ctest_result\n"
  ")\n"
  "if(NOT \${_ctest_result} EQUAL 0)\n"
  "  message(WARNING\n"
  "    \"ctest exited with code \${_ctest_result}; generating coverage report anyway.\")\n"
  "endif()\n"
  "\n"
  "# Step 2: run gcovr unconditionally to produce HTML and JSON reports.\n"
  "file(MAKE_DIRECTORY \"${CMAKE_BINARY_DIR}/coverage-report\")\n"
  "execute_process(\n"
  "  COMMAND \"${GCOVR_PATH}\"\n"
  "          --gcov-executable \"${_gcov_executable}\"\n"
  "          --root \"${CMAKE_SOURCE_DIR}\"\n"
  "          --filter \"/.*/src/G4OCCT[^/]*\"\n"
  "          --filter \"/.*/include/G4OCCT/.*\"\n"
  "          --json-summary \"${CMAKE_BINARY_DIR}/coverage-summary.json\"\n"
  "          --print-summary\n"
  "          --html\n"
  "          --html-details\n"
  "          --output \"${CMAKE_BINARY_DIR}/coverage-report/index.html\"\n"
  "  RESULT_VARIABLE _gcovr_result\n"
  ")\n"
  "if(NOT \${_gcovr_result} EQUAL 0)\n"
  "  message(FATAL_ERROR \"gcovr failed with exit code \${_gcovr_result}\")\n"
  "endif()\n"
  "\n"
  "# Step 3: propagate ctest failure after coverage is fully generated.\n"
  "if(NOT \${_ctest_result} EQUAL 0)\n"
  "  message(FATAL_ERROR \"ctest failed with exit code \${_ctest_result}\")\n"
  "endif()\n")

add_custom_target(
  coverage-report
  COMMAND "${CMAKE_COMMAND}" -P "${_coverage_runner}"
  COMMENT "Running tests and generating coverage report with gcovr"
  VERBATIM)
