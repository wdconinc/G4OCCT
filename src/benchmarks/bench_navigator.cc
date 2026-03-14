// bench_navigator.cc
// Navigator performance benchmark: compare geantino tracking through
// identical geometries described in native Geant4 vs G4OCCTSolid wrappers.
//
// Usage:
//   ./bench_navigator [N_geantinos]   (default: 10000)
//
// The benchmark creates a simple world geometry containing a set of primitive
// volumes (box, sphere, cylinder) placed in a grid.  The same geometry is
// built twice:
//   1. Using native Geant4 solids (G4Box, G4Sphere, G4Tubs).
//   2. Using G4OCCTSolid wrappers around equivalent OCCT BRep shapes.
//
// A geantino (charge-free, mass-free test particle) is shot from a random
// origin along a random direction through each geometry and the number of
// boundary crossings is recorded.  The elapsed wall-clock time for N geantinos
// is reported for both cases.
//
// NOTE: The OCCT-based navigation is currently a stub; this benchmark
//       infrastructure will be completed once the G4OCCTSolid navigation
//       methods are fully implemented.

#include "G4OCCT/G4OCCTSolid.hh"
#include "G4OCCT/G4OCCTLogicalVolume.hh"
#include "G4OCCT/G4OCCTPlacement.hh"

// Geant4 native solids
#include "G4Box.hh"
#include "G4Sphere.hh"
#include "G4Tubs.hh"
#include "G4NistManager.hh"
#include "G4Navigator.hh"
#include "G4GeometryManager.hh"

// OCCT primitives
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

// ── geometry builders ─────────────────────────────────────────────────────────

/// Build a native-Geant4 world with a box, sphere, and cylinder inside.
G4VPhysicalVolume* BuildNativeGeometry(G4Material* air, G4Material* iron) {
  auto* worldSolid = new G4Box("WorldBox", 1000, 1000, 1000);
  auto* worldLV    = new G4LogicalVolume(worldSolid, air, "WorldLV");
  auto* worldPV    = new G4PVPlacement(
      nullptr, G4ThreeVector(), worldLV, "World", nullptr, false, 0);

  auto* boxSolid = new G4Box("Box", 50, 50, 50);
  auto* boxLV    = new G4LogicalVolume(boxSolid, iron, "BoxLV");
  new G4PVPlacement(nullptr, G4ThreeVector(-200, 0, 0), boxLV, "BoxPV",
                    worldLV, false, 0);

  auto* sphereSolid = new G4Sphere("Sphere", 0, 50, 0,
                                   CLHEP::twopi, 0, CLHEP::pi);
  auto* sphereLV    = new G4LogicalVolume(sphereSolid, iron, "SphereLV");
  new G4PVPlacement(nullptr, G4ThreeVector(0, 0, 0), sphereLV, "SpherePV",
                    worldLV, false, 0);

  auto* cylSolid = new G4Tubs("Cylinder", 0, 40, 60, 0, CLHEP::twopi);
  auto* cylLV    = new G4LogicalVolume(cylSolid, iron, "CylLV");
  new G4PVPlacement(nullptr, G4ThreeVector(200, 0, 0), cylLV, "CylPV",
                    worldLV, false, 0);

  return worldPV;
}

/// Build an equivalent world using G4OCCTSolid wrappers.
G4VPhysicalVolume* BuildOCCTGeometry(G4Material* air, G4Material* iron) {
  TopoDS_Shape worldShape = BRepPrimAPI_MakeBox(2000, 2000, 2000).Shape();
  auto* worldSolid = new G4OCCTSolid("WorldBox", worldShape);
  auto* worldLV    = new G4OCCTLogicalVolume(worldSolid, air, "WorldLV_OCCT",
                                             worldShape);
  auto* worldPV    = new G4OCCTPlacement(
      nullptr, G4ThreeVector(), worldLV, "World_OCCT", nullptr, false, 0);

  TopoDS_Shape boxShape = BRepPrimAPI_MakeBox(100, 100, 100).Shape();
  auto* boxSolid = new G4OCCTSolid("Box", boxShape);
  auto* boxLV    = new G4OCCTLogicalVolume(boxSolid, iron, "BoxLV_OCCT",
                                           boxShape);
  new G4OCCTPlacement(nullptr, G4ThreeVector(-200, 0, 0), boxLV, "BoxPV_OCCT",
                      worldLV, false, 0);

  TopoDS_Shape sphereShape = BRepPrimAPI_MakeSphere(50).Shape();
  auto* sphereSolid = new G4OCCTSolid("Sphere", sphereShape);
  auto* sphereLV    = new G4OCCTLogicalVolume(sphereSolid, iron,
                                              "SphereLV_OCCT", sphereShape);
  new G4OCCTPlacement(nullptr, G4ThreeVector(0, 0, 0), sphereLV,
                      "SpherePV_OCCT", worldLV, false, 0);

  TopoDS_Shape cylShape = BRepPrimAPI_MakeCylinder(40, 120).Shape();
  auto* cylSolid = new G4OCCTSolid("Cylinder", cylShape);
  auto* cylLV    = new G4OCCTLogicalVolume(cylSolid, iron, "CylLV_OCCT",
                                           cylShape);
  new G4OCCTPlacement(nullptr, G4ThreeVector(200, 0, 0), cylLV,
                      "CylPV_OCCT", worldLV, false, 0);

  return worldPV;
}

// ── benchmark runner ─────────────────────────────────────────────────────────

long RunGeantinos(G4VPhysicalVolume* world, int nGeantinos) {
  G4GeometryManager::GetInstance()->OpenGeometry(world);
  G4GeometryManager::GetInstance()->CloseGeometry(true, false, world);

  G4Navigator nav;
  nav.SetWorldVolume(world);

  std::mt19937 rng(42);
  std::uniform_real_distribution<double> posRange(-900, 900);
  std::uniform_real_distribution<double> dirRange(-1, 1);

  long totalSteps = 0;

  for (int i = 0; i < nGeantinos; ++i) {
    G4ThreeVector pos(posRange(rng), posRange(rng), posRange(rng));
    G4ThreeVector dir(dirRange(rng), dirRange(rng), dirRange(rng));
    dir = dir.unit();

    nav.LocateGlobalPointAndSetup(pos);

    int steps = 0;
    double safety = 0;
    for (int s = 0; s < 200; ++s) {
      double step = 50.0;
      double dist = nav.ComputeStep(pos, dir, step, safety);
      if (dist >= kInfinity) { break; }
      pos += (dist + 1e-7) * dir;
      nav.SetGeometricallyLimitedStep();
      nav.LocateGlobalPointAndSetup(pos);
      ++steps;
    }
    totalSteps += steps;
  }

  G4GeometryManager::GetInstance()->OpenGeometry(world);
  return totalSteps;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
  int nGeantinos = 10000;
  if (argc > 1) { nGeantinos = std::atoi(argv[1]); }

  G4Material* air  = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  G4Material* iron = G4NistManager::Instance()->FindOrBuildMaterial("G4_Fe");

  std::cout << "Building native Geant4 geometry...\n";
  G4VPhysicalVolume* nativeWorld = BuildNativeGeometry(air, iron);

  std::cout << "Building OCCT-wrapped geometry...\n";
  G4VPhysicalVolume* occtWorld = BuildOCCTGeometry(air, iron);

  // ── native benchmark ───────────────────────────────────────────────────────
  std::cout << "Running " << nGeantinos
            << " geantinos through native geometry...\n";
  auto t0 = std::chrono::steady_clock::now();
  long stepsNative = RunGeantinos(nativeWorld, nGeantinos);
  auto t1 = std::chrono::steady_clock::now();
  double msNative =
      std::chrono::duration<double, std::milli>(t1 - t0).count();

  // ── OCCT benchmark ─────────────────────────────────────────────────────────
  std::cout << "Running " << nGeantinos
            << " geantinos through OCCT geometry (stub)...\n";
  auto t2 = std::chrono::steady_clock::now();
  long stepsOCCT = RunGeantinos(occtWorld, nGeantinos);
  auto t3 = std::chrono::steady_clock::now();
  double msOCCT =
      std::chrono::duration<double, std::milli>(t3 - t2).count();

  // ── results ────────────────────────────────────────────────────────────────
  std::cout << "\n=== Navigator Benchmark Results ===\n";
  std::cout << "Geantinos: " << nGeantinos << "\n";
  std::cout << "Native Geant4 : " << msNative  << " ms  (" << stepsNative
            << " steps)\n";
  std::cout << "OCCT (stub)   : " << msOCCT    << " ms  (" << stepsOCCT
            << " steps)\n";
  if (msOCCT > 0) {
    std::cout << "Speed ratio (native/OCCT): " << msNative / msOCCT << "\n";
  }

  return 0;
}
