# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

# check_nonempty.cmake — CTest helper that verifies FILE has at least two lines
# (header + one data row).  Invoked with: cmake -D FILE=<path> -P
# check_nonempty.cmake

if(NOT DEFINED FILE)
  message(FATAL_ERROR "FILE variable is not set")
endif()

if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "File does not exist: ${FILE}")
endif()

file(STRINGS "${FILE}" _lines)
list(LENGTH _lines _n)
if(_n LESS 2)
  message(
    FATAL_ERROR
      "File has ${_n} line(s) — expected at least 2 (header + one data row): ${FILE}"
  )
endif()
