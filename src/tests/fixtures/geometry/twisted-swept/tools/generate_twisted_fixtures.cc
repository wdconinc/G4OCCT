// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

/// Number of cross-sections for B-spline lofting through twisted profiles.
/// 64 sections give sub-degree angular steps for the 30° twist used in all fixtures.
constexpr int kLoftSectionCount = 64;

struct SectionPoint {
  double x;
  double y;
};

constexpr double kPi = 3.14159265358979323846;

std::pair<double, double> Rotate2D(const double x, const double y, const double angle_deg) {
  const double angle_rad = angle_deg * kPi / 180.0;
  return {x * std::cos(angle_rad) - y * std::sin(angle_rad),
          x * std::sin(angle_rad) + y * std::cos(angle_rad)};
}

TopoDS_Wire MakePolygonWire(const std::vector<SectionPoint>& section, const double z,
                            const double angle_deg, const double shift_x = 0.0,
                            const double shift_y = 0.0) {
  BRepBuilderAPI_MakePolygon polygon;
  for (const auto& point : section) {
    const auto [xr, yr] = Rotate2D(point.x, point.y, angle_deg);
    polygon.Add(gp_Pnt(xr + shift_x, yr + shift_y, z));
  }
  polygon.Close();
  return polygon.Wire();
}

TopoDS_Shape MakeBSplineLoftSolid(const std::vector<TopoDS_Wire>& wires) {
  BRepOffsetAPI_ThruSections loft(/*isSolid=*/true, /*isRuled=*/false);
  loft.CheckCompatibility(false);
  for (const auto& wire : wires) {
    loft.AddWire(wire);
  }
  loft.Build();
  if (!loft.IsDone()) {
    throw std::runtime_error("Failed to build B-spline loft solid");
  }
  return loft.Shape();
}

TopoDS_Wire MakeTwistedTubsWire(const double z, const double angle_offset_deg,
                                const double inner_radius, const double outer_radius,
                                const double total_phi_deg) {
  const double phi1 = (-0.5 * total_phi_deg + angle_offset_deg) * kPi / 180.0;
  const double phi2 = (0.5 * total_phi_deg + angle_offset_deg) * kPi / 180.0;

  gp_Ax2 axis(gp_Pnt(0.0, 0.0, z), gp_Dir(0.0, 0.0, 1.0));
  gp_Circ outer_circle(axis, outer_radius);
  gp_Circ inner_circle(axis, inner_radius);

  const gp_Pnt outer_start(outer_radius * std::cos(phi1), outer_radius * std::sin(phi1), z);
  const gp_Pnt outer_end(outer_radius * std::cos(phi2), outer_radius * std::sin(phi2), z);
  const gp_Pnt inner_start(inner_radius * std::cos(phi1), inner_radius * std::sin(phi1), z);
  const gp_Pnt inner_end(inner_radius * std::cos(phi2), inner_radius * std::sin(phi2), z);

  const Handle(Geom_TrimmedCurve) outer_arc =
      GC_MakeArcOfCircle(outer_start, gp_Pnt(outer_radius, 0.0, z), outer_end);
  const Handle(Geom_TrimmedCurve) inner_arc =
      GC_MakeArcOfCircle(inner_end, gp_Pnt(inner_radius, 0.0, z), inner_start);

  BRepBuilderAPI_MakeWire wire;
  wire.Add(BRepBuilderAPI_MakeEdge(outer_arc));
  wire.Add(BRepBuilderAPI_MakeEdge(outer_end, inner_end));
  wire.Add(BRepBuilderAPI_MakeEdge(inner_arc));
  wire.Add(BRepBuilderAPI_MakeEdge(inner_start, outer_start));
  return wire.Wire();
}

TopoDS_Shape MakeTwistedBox() {
  const double dz  = 20.0;
  const double phi = 30.0;
  const std::vector<SectionPoint> section{{-10.0, -8.0}, {10.0, -8.0}, {10.0, 8.0}, {-10.0, 8.0}};
  std::vector<TopoDS_Wire> wires;
  wires.reserve(kLoftSectionCount);
  for (int i = 0; i < kLoftSectionCount; ++i) {
    const double t       = static_cast<double>(i) / (kLoftSectionCount - 1);
    const double z_i     = -dz + t * 2.0 * dz;
    const double theta_i = phi * z_i / (2.0 * dz);
    wires.push_back(MakePolygonWire(section, z_i, theta_i));
  }
  return MakeBSplineLoftSolid(wires);
}

TopoDS_Shape MakeTwistedTrd() {
  const double dz  = 20.0;
  const double phi = 30.0;
  const std::vector<SectionPoint> bottom{{-10.0, -8.0}, {10.0, -8.0}, {10.0, 8.0}, {-10.0, 8.0}};
  const std::vector<SectionPoint> top{{-16.0, -14.0}, {16.0, -14.0}, {16.0, 14.0}, {-16.0, 14.0}};
  std::vector<TopoDS_Wire> wires;
  wires.reserve(kLoftSectionCount);
  for (int i = 0; i < kLoftSectionCount; ++i) {
    const double t       = static_cast<double>(i) / (kLoftSectionCount - 1);
    const double z_i     = -dz + t * 2.0 * dz;
    const double theta_i = phi * z_i / (2.0 * dz);
    std::vector<SectionPoint> section(bottom.size());
    for (std::size_t j = 0; j < bottom.size(); ++j) {
      section[j] = {bottom[j].x + t * (top[j].x - bottom[j].x),
                    bottom[j].y + t * (top[j].y - bottom[j].y)};
    }
    wires.push_back(MakePolygonWire(section, z_i, theta_i));
  }
  return MakeBSplineLoftSolid(wires);
}

TopoDS_Shape MakeTwistedTrap() {
  const double dz  = 18.0;
  const double phi = 30.0;
  const std::vector<SectionPoint> section{{-7.0, -9.0}, {7.0, -9.0}, {13.0, 9.0}, {-13.0, 9.0}};
  std::vector<TopoDS_Wire> wires;
  wires.reserve(kLoftSectionCount);
  for (int i = 0; i < kLoftSectionCount; ++i) {
    const double t       = static_cast<double>(i) / (kLoftSectionCount - 1);
    const double z_i     = -dz + t * 2.0 * dz;
    const double theta_i = phi * z_i / (2.0 * dz);
    wires.push_back(MakePolygonWire(section, z_i, theta_i));
  }
  return MakeBSplineLoftSolid(wires);
}

TopoDS_Shape MakeGenericTwistedFaceted() {
  const double dz      = 20.0;
  const double phi     = 30.0;
  const double theta   = 8.0 * kPi / 180.0;
  const double azimuth = 20.0 * kPi / 180.0;
  const double shift_r = 2.0 * dz * std::tan(theta);
  const double shift_x = shift_r * std::cos(azimuth);
  const double shift_y = shift_r * std::sin(azimuth);
  const double alpha   = 12.0 * kPi / 180.0;

  const std::vector<SectionPoint> bottom{{-9.0, -8.0}, {9.0, -8.0}, {12.0, 8.0}, {-12.0, 8.0}};
  const std::vector<SectionPoint> top{{-7.0 - 10.0 * std::tan(alpha), -10.0},
                                      {7.0 - 10.0 * std::tan(alpha), -10.0},
                                      {13.0 + 10.0 * std::tan(alpha), 10.0},
                                      {-13.0 + 10.0 * std::tan(alpha), 10.0}};
  std::vector<TopoDS_Wire> wires;
  wires.reserve(kLoftSectionCount);
  for (int i = 0; i < kLoftSectionCount; ++i) {
    const double t       = static_cast<double>(i) / (kLoftSectionCount - 1);
    const double z_i     = -dz + t * 2.0 * dz;
    const double theta_i = phi * z_i / (2.0 * dz);
    std::vector<SectionPoint> section(bottom.size());
    for (std::size_t j = 0; j < bottom.size(); ++j) {
      section[j] = {bottom[j].x + t * (top[j].x - bottom[j].x),
                    bottom[j].y + t * (top[j].y - bottom[j].y)};
    }
    wires.push_back(MakePolygonWire(section, z_i, theta_i, t * shift_x, t * shift_y));
  }
  return MakeBSplineLoftSolid(wires);
}

TopoDS_Shape MakeTwistedTubs() {
  const double dz   = 20.0;
  const double phi  = 30.0;
  const double dphi = 210.0;
  const double rmin = 6.0;
  const double rmax = 12.0;
  std::vector<TopoDS_Wire> wires;
  wires.reserve(kLoftSectionCount);
  for (int i = 0; i < kLoftSectionCount; ++i) {
    const double t              = static_cast<double>(i) / (kLoftSectionCount - 1);
    const double z_i            = -dz + t * 2.0 * dz;
    const double angle_offset_i = phi * z_i / (2.0 * dz);
    wires.push_back(MakeTwistedTubsWire(z_i, angle_offset_i, rmin, rmax, dphi));
  }
  return MakeBSplineLoftSolid(wires);
}

TopoDS_Shape MakeFixture(const std::string& fixture_name) {
  if (fixture_name == "twisted-box")
    return MakeTwistedBox();
  if (fixture_name == "twisted-trd")
    return MakeTwistedTrd();
  if (fixture_name == "twisted-trap")
    return MakeTwistedTrap();
  if (fixture_name == "twisted-tubs")
    return MakeTwistedTubs();
  if (fixture_name == "vtwisted-faceted")
    return MakeGenericTwistedFaceted();
  throw std::runtime_error("Unknown twisted fixture name: " + fixture_name);
}

void ValidateAndWrite(const TopoDS_Shape& shape, const std::filesystem::path& output_path) {
  BRepCheck_Analyzer analyzer(shape);
  if (!analyzer.IsValid()) {
    throw std::runtime_error("Generated twisted shape is invalid");
  }
  STEPControl_Writer writer;
  if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
    throw std::runtime_error("Failed to transfer twisted shape to STEP writer");
  }
  if (writer.Write(output_path.string().c_str()) != IFSelect_RetDone) {
    throw std::runtime_error("Failed to write STEP file: " + output_path.string());
  }
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  std::cout << props.Mass() << '\n';
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: generate_twisted_fixtures <fixture-name> <output.step>\n";
    return EXIT_FAILURE;
  }
  try {
    const TopoDS_Shape shape = MakeFixture(argv[1]);
    ValidateAndWrite(shape, argv[2]);
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
