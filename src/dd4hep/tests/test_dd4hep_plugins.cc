// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file test_dd4hep_plugins.cc
/// @brief GTest-based integration tests for the G4OCCT DD4hep detector plugins.
///
/// Each test loads a compact XML file into a fresh DD4hep Detector instance,
/// inspects the resulting geometry tree, and verifies:
///  - The expected number of placed volumes.
///  - Material assignment for the placed volumes.
///  - No DD4hep or Geant4 exceptions during construction.
///
/// Tests are labelled "dd4hep" (via CTest LABELS) so they can be run in
/// isolation with `ctest -L dd4hep` or excluded from other runs with
/// `ctest -LE dd4hep`.

#include <DD4hep/Detector.h>
#include <DD4hep/DetElement.h>
#include <DD4hep/Volumes.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

// ── Paths injected by CMake configure_file() ─────────────────────────────────

#ifndef G4OCCT_COMPACT_STEP_SOLID
#error "G4OCCT_COMPACT_STEP_SOLID must be defined by CMake"
#endif
#ifndef G4OCCT_COMPACT_STEP_ASSEMBLY
#error "G4OCCT_COMPACT_STEP_ASSEMBLY must be defined by CMake"
#endif

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

/// Load a compact XML into a fresh Detector instance.
/// Returns a reference to the singleton that the caller must destroy.
dd4hep::Detector& LoadCompact(const std::string& path) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Compact XML not found: " + path);
  }
  dd4hep::Detector& det = dd4hep::Detector::getInstance();
  det.fromCompact(path);
  return det;
}

/// Destroy and reset the Detector singleton between tests.
/// In DD4hep, destroyInstance() handles both destruction and state reset.
void DestroyDetector() { dd4hep::Detector::destroyInstance(); }

} // namespace

// ── STEPSolid tests ──────────────────────────────────────────────────────────

class STEPSolidTest : public ::testing::Test {
protected:
  void TearDown() override { DestroyDetector(); }
};

/// Verify that G4OCCT_STEPSolid creates a detector element without errors.
TEST_F(STEPSolidTest, LoadFromCompact) {
  ASSERT_NO_THROW(LoadCompact(G4OCCT_COMPACT_STEP_SOLID));
  dd4hep::Detector& det = dd4hep::Detector::getInstance();

  // The compact file declares detector id=1 name="TestBox".
  const dd4hep::DetElement& world = det.world();
  ASSERT_TRUE(world.isValid()) << "World DetElement is invalid";

  // "TestBox" should be a direct child of the world.
  auto children = world.children();
  EXPECT_GT(children.size(), 0u) << "No detector elements placed in world";

  bool found = false;
  for (const auto& [childName, childEl] : children) {
    if (childName == "TestBox") {
      found = true;
      EXPECT_TRUE(childEl.isValid());
      EXPECT_TRUE(childEl.placement().isValid()) << "TestBox placement is invalid";
    }
  }
  EXPECT_TRUE(found) << "DetElement 'TestBox' not found among world children";
}

/// Verify that the placed volume has a valid material.
TEST_F(STEPSolidTest, MaterialAssignment) {
  ASSERT_NO_THROW(LoadCompact(G4OCCT_COMPACT_STEP_SOLID));
  dd4hep::Detector& det = dd4hep::Detector::getInstance();

  const auto& children = det.world().children();
  auto it              = children.find("TestBox");
  ASSERT_NE(it, children.end()) << "DetElement 'TestBox' not found";

  dd4hep::PlacedVolume pv = it->second.placement();
  ASSERT_TRUE(pv.isValid());

  dd4hep::Volume vol = pv.volume();
  ASSERT_TRUE(vol.isValid());
  EXPECT_TRUE(vol.material().isValid()) << "Volume material is invalid";
}

// ── STEPAssembly tests ────────────────────────────────────────────────────────

class STEPAssemblyTest : public ::testing::Test {
protected:
  void TearDown() override { DestroyDetector(); }
};

/// Verify that G4OCCT_STEPAssembly creates a detector element without errors.
TEST_F(STEPAssemblyTest, LoadFromCompact) {
  ASSERT_NO_THROW(LoadCompact(G4OCCT_COMPACT_STEP_ASSEMBLY));
  dd4hep::Detector& det = dd4hep::Detector::getInstance();

  const dd4hep::DetElement& world = det.world();
  ASSERT_TRUE(world.isValid());

  auto children = world.children();
  EXPECT_GT(children.size(), 0u) << "No detector elements placed in world";

  bool found = false;
  for (const auto& [childName, childEl] : children) {
    if (childName == "TripleBoxAssembly") {
      found = true;
      EXPECT_TRUE(childEl.isValid());
      EXPECT_TRUE(childEl.placement().isValid()) << "TripleBoxAssembly placement is invalid";
    }
  }
  EXPECT_TRUE(found) << "DetElement 'TripleBoxAssembly' not found among world children";
}
