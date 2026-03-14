cmake_minimum_required(VERSION 3.16)

# ─── test_solid ───────────────────────────────────────────────────────────────
add_executable(test_solid test_solid.cc)
target_link_libraries(test_solid PRIVATE G4OCCT)
add_test(NAME test_solid COMMAND test_solid)

# ─── test_logical_volume ──────────────────────────────────────────────────────
add_executable(test_logical_volume test_logical_volume.cc)
target_link_libraries(test_logical_volume PRIVATE G4OCCT)
add_test(NAME test_logical_volume COMMAND test_logical_volume)

# ─── test_placement ───────────────────────────────────────────────────────────
add_executable(test_placement test_placement.cc)
target_link_libraries(test_placement PRIVATE G4OCCT)
add_test(NAME test_placement COMMAND test_placement)
