// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_solid_builder.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_manifest.hh"

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopoDS_Shape.hxx>

#include <G4Box.hh>
#include <G4Cons.hh>
#include <G4CutTubs.hh>
#include <G4DisplacedSolid.hh>
#include <G4Ellipsoid.hh>
#include <G4EllipticalCone.hh>
#include <G4EllipticalTube.hh>
#include <G4ExtrudedSolid.hh>
#include <G4GenericPolycone.hh>
#include <G4GenericTrap.hh>
#include <G4Hype.hh>
#include <G4IntersectionSolid.hh>
#include <G4MultiUnion.hh>
#include <G4Orb.hh>
#include <G4Para.hh>
#include <G4Paraboloid.hh>
#include <G4Polycone.hh>
#include <G4Polyhedra.hh>
#include <G4RotationMatrix.hh>
#include <G4ScaledSolid.hh>
#include <G4Sphere.hh>
#include <G4SubtractionSolid.hh>
#include <G4SystemOfUnits.hh>
#include <G4TessellatedSolid.hh>
#include <G4Tet.hh>
#include <G4Torus.hh>
#include <G4Transform3D.hh>
#include <G4Trap.hh>
#include <G4TriangularFacet.hh>
#include <G4Trd.hh>
#include <G4Tubs.hh>
#include <G4TwistedBox.hh>
#include <G4TwistedTrap.hh>
#include <G4TwistedTrd.hh>
#include <G4TwistedTubs.hh>
#include <G4TwoVector.hh>
#include <G4UnionSolid.hh>
#include <G4VTwistedFaceted.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace g4occt::tests::geometry {
namespace {

  constexpr double kGoldenAngle = 2.39996322972865332;

  // ──────────────────────────────────────────────────────────────────────────
  // Minimal YAML parsing helpers
  // ──────────────────────────────────────────────────────────────────────────

  const YAML::Node RequireNode(const YAML::Node& parent, const std::string& key,
                               const std::string& context) {
    const YAML::Node node = parent[key];
    if (!node.IsDefined()) {
      throw std::runtime_error("Missing YAML key '" + key + "' in " + context);
    }
    return node;
  }

  bool HasNode(const YAML::Node& parent, const std::string& key) { return parent[key].IsDefined(); }

  double ParseDouble(const YAML::Node& node, const std::string& context) {
    try {
      return std::stod(node.as<std::string>());
    } catch (const std::exception&) {
      throw std::runtime_error("Expected numeric YAML scalar in " + context + ": '" +
                               node.as<std::string>() + "'");
    }
  }

  std::string RequireString(const YAML::Node& parent, const std::string& key,
                            const std::string& context) {
    return RequireNode(parent, key, context).as<std::string>();
  }

  double RequireDouble(const YAML::Node& parent, const std::string& key,
                       const std::string& context) {
    return ParseDouble(RequireNode(parent, key, context), context + "." + key);
  }

  std::vector<double> RequireDoubleList(const YAML::Node& parent, const std::string& key,
                                        const std::string& context) {
    const YAML::Node sequence = RequireNode(parent, key, context);
    std::vector<double> values;
    values.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
      values.push_back(
          ParseDouble(sequence[index], context + "." + key + "[" + std::to_string(index) + "]"));
    }
    return values;
  }

  std::vector<G4ThreeVector> RequirePointList(const YAML::Node& parent, const std::string& key,
                                              const std::string& context) {
    const YAML::Node sequence = RequireNode(parent, key, context);
    std::vector<G4ThreeVector> points;
    points.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
      const YAML::Node coords = sequence[index];
      if (coords.size() != 3U) {
        throw std::runtime_error("Expected 3-vector YAML entry in " + context + "." + key + "[" +
                                 std::to_string(index) + "]");
      }
      points.emplace_back(ParseDouble(coords[0], context), ParseDouble(coords[1], context),
                          ParseDouble(coords[2], context));
    }
    return points;
  }

  std::vector<G4TwoVector> RequirePlanarPointList(const YAML::Node& parent, const std::string& key,
                                                  const std::string& context) {
    const YAML::Node sequence = RequireNode(parent, key, context);
    std::vector<G4TwoVector> points;
    points.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
      const YAML::Node coords = sequence[index];
      if (coords.size() != 2U) {
        throw std::runtime_error("Expected 2-vector YAML entry in " + context + "." + key + "[" +
                                 std::to_string(index) + "]");
      }
      points.emplace_back(ParseDouble(coords[0], context), ParseDouble(coords[1], context));
    }
    return points;
  }

  double SignedArea(const std::vector<G4TwoVector>& polygon) {
    if (polygon.size() < 3U) {
      return 0.0;
    }

    double area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
      const G4TwoVector& current = polygon[index];
      const G4TwoVector& next    = polygon[(index + 1U) % polygon.size()];
      area += current.x() * next.y() - next.x() * current.y();
    }
    return 0.5 * area;
  }

  std::vector<G4TwoVector> EnsureClockwise(std::vector<G4TwoVector> polygon) {
    if (SignedArea(polygon) > 0.0) {
      std::reverse(polygon.begin(), polygon.end());
    }
    return polygon;
  }

  G4ThreeVector RequireVector3(const YAML::Node& parent, const std::string& key,
                               const std::string& context) {
    const std::vector<double> values = RequireDoubleList(parent, key, context);
    if (values.size() != 3U) {
      throw std::runtime_error("Expected 3 numeric values in " + context + "." + key);
    }
    return {values[0], values[1], values[2]};
  }

  YAML::Node ShapeNode(const FixtureProvenance& provenance) {
    return RequireNode(provenance.document, "shape", provenance.source_path.string());
  }

  // ──────────────────────────────────────────────────────────────────────────
  // Sub-builders for complex solid types
  // ──────────────────────────────────────────────────────────────────────────

  std::unique_ptr<G4VSolid> BuildBooleanBoxSolid(const std::string& geant4_class,
                                                 const std::string& name, const YAML::Node& shape,
                                                 const std::string& context) {
    if (geant4_class == "G4UnionSolid" || geant4_class == "G4IntersectionSolid") {
      if (geant4_class == "G4IntersectionSolid" && HasNode(shape, "overlap_box_mm")) {
        const YAML::Node overlap         = RequireNode(shape, "overlap_box_mm", context);
        const G4ThreeVector overlap_size = RequireVector3(overlap, "size", context);
        return std::make_unique<G4Box>(name, 0.5 * overlap_size.x(), 0.5 * overlap_size.y(),
                                       0.5 * overlap_size.z());
      }

      const YAML::Node boxes = RequireNode(shape, "component_boxes_mm", context);
      if (boxes.size() != 2U) {
        throw std::runtime_error(context +
                                 ": boolean fixture requires exactly two component boxes");
      }
      const YAML::Node left          = boxes[0];
      const YAML::Node right         = boxes[1];
      const G4ThreeVector left_min   = RequireVector3(left, "min", context);
      const G4ThreeVector left_size  = RequireVector3(left, "size", context);
      const G4ThreeVector right_min  = RequireVector3(right, "min", context);
      const G4ThreeVector right_size = RequireVector3(right, "size", context);

      auto* left_box =
          new G4Box(name + "_left", 0.5 * left_size.x(), 0.5 * left_size.y(), 0.5 * left_size.z());
      auto* right_box = new G4Box(name + "_right", 0.5 * right_size.x(), 0.5 * right_size.y(),
                                  0.5 * right_size.z());
      const G4ThreeVector left_center  = left_min + 0.5 * left_size;
      const G4ThreeVector right_center = right_min + 0.5 * right_size;
      const G4ThreeVector translation  = right_center - left_center;
      if (geant4_class == "G4UnionSolid") {
        return std::make_unique<G4UnionSolid>(name, left_box, right_box, nullptr, translation);
      }
      return std::make_unique<G4IntersectionSolid>(name, left_box, right_box, nullptr, translation);
    }

    if (geant4_class == "G4SubtractionSolid") {
      const std::vector<double> outer = RequireDoubleList(shape, "outer_box_mm", context);
      const std::vector<double> removed_min =
          RequireDoubleList(shape, "removed_box_min_mm", context);
      const std::vector<double> removed_size =
          RequireDoubleList(shape, "removed_box_size_mm", context);
      if (outer.size() != 3U || removed_min.size() != 3U || removed_size.size() != 3U) {
        throw std::runtime_error(context + ": subtraction-solid box metadata must be 3-vectors");
      }
      auto* outer_box = new G4Box(name + "_outer", 0.5 * outer[0], 0.5 * outer[1], 0.5 * outer[2]);
      auto* removed_box = new G4Box(name + "_removed", 0.5 * removed_size[0], 0.5 * removed_size[1],
                                    0.5 * removed_size[2]);
      // removed_box_min_mm stores the world-frame position of the removed box after the
      // copytranslate that centres the overall shape. The outer_box is centred at the G4 origin,
      // so the translation of the removed solid is just its own centre in world coordinates.
      const G4ThreeVector removed_center(removed_min[0] + 0.5 * removed_size[0],
                                         removed_min[1] + 0.5 * removed_size[1],
                                         removed_min[2] + 0.5 * removed_size[2]);
      return std::make_unique<G4SubtractionSolid>(name, outer_box, removed_box, nullptr,
                                                  removed_center);
    }

    throw std::runtime_error("Unsupported boolean class in " + context + ": " + geant4_class);
  }

  std::unique_ptr<G4VSolid> BuildMultiUnionSolid(const std::string& name, const YAML::Node& shape,
                                                 const std::string& context) {
    auto solid             = std::make_unique<G4MultiUnion>(name);
    const YAML::Node boxes = RequireNode(shape, "component_boxes_mm", context);
    for (std::size_t index = 0; index < boxes.size(); ++index) {
      const YAML::Node box_node   = boxes[index];
      const G4ThreeVector minimum = RequireVector3(box_node, "min", context);
      const G4ThreeVector size    = RequireVector3(box_node, "size", context);
      auto* box = new G4Box(name + "_box_" + std::to_string(index), 0.5 * size.x(), 0.5 * size.y(),
                            0.5 * size.z());
      G4Transform3D transform(G4RotationMatrix(), minimum + 0.5 * size);
      solid->AddNode(*box, transform);
    }
    solid->Voxelize();
    return solid;
  }

  std::unique_ptr<G4VSolid> BuildTessellatedSolid(const std::string& name, const YAML::Node& shape,
                                                  const std::string& context) {
    const std::vector<G4ThreeVector> vertices = RequirePointList(shape, "vertices_mm", context);
    const YAML::Node facets                   = RequireNode(shape, "triangular_facets", context);

    auto solid = std::make_unique<G4TessellatedSolid>(name);
    for (std::size_t facet_index = 0; facet_index < facets.size(); ++facet_index) {
      const YAML::Node indices = facets[facet_index];
      if (indices.size() != 3U) {
        throw std::runtime_error(context +
                                 ": triangular_facets entries must contain exactly 3 indices");
      }
      const int i0 = static_cast<int>(ParseDouble(indices[0], context));
      const int i1 = static_cast<int>(ParseDouble(indices[1], context));
      const int i2 = static_cast<int>(ParseDouble(indices[2], context));
      solid->AddFacet(new G4TriangularFacet(vertices.at(static_cast<std::size_t>(i0)),
                                            vertices.at(static_cast<std::size_t>(i1)),
                                            vertices.at(static_cast<std::size_t>(i2)), ABSOLUTE));
    }
    solid->SetSolidClosed(true);
    return solid;
  }

  void AddOrientedTriangle(G4TessellatedSolid* solid, const G4ThreeVector& center,
                           const G4ThreeVector& a, const G4ThreeVector& b, const G4ThreeVector& c) {
    const G4ThreeVector face_center = (a + b + c) / 3.0;
    const G4ThreeVector normal      = (b - a).cross(c - a);
    if (normal.dot(face_center - center) < 0.0) {
      solid->AddFacet(new G4TriangularFacet(a, c, b, ABSOLUTE));
    } else {
      solid->AddFacet(new G4TriangularFacet(a, b, c, ABSOLUTE));
    }
  }

  std::unique_ptr<G4VSolid> BuildVTwistedFacetedFallback(const std::string& name,
                                                         const YAML::Node& ctor,
                                                         const std::string& context) {
    const G4double phi_twist = RequireDouble(ctor, "phi_twist_deg", context) * deg;
    const G4double dz        = RequireDouble(ctor, "dz_mm", context);
    const G4double theta     = RequireDouble(ctor, "theta_deg", context) * deg;
    const G4double phi       = RequireDouble(ctor, "phi_deg", context) * deg;
    const G4double dy1       = RequireDouble(ctor, "dy1_mm", context);
    const G4double dx1       = RequireDouble(ctor, "dx1_mm", context);
    const G4double dx2       = RequireDouble(ctor, "dx2_mm", context);
    const G4double dy2       = RequireDouble(ctor, "dy2_mm", context);
    const G4double dx3       = RequireDouble(ctor, "dx3_mm", context);
    const G4double dx4       = RequireDouble(ctor, "dx4_mm", context);
    const G4double alpha     = RequireDouble(ctor, "alpha_deg", context) * deg;

    const auto rotate = [](const G4double x, const G4double y, const G4double angle) {
      return G4TwoVector(x * std::cos(angle) - y * std::sin(angle),
                         x * std::sin(angle) + y * std::cos(angle));
    };

    const G4double shift_radius = 2.0 * dz * std::tan(theta);
    const G4double shift_x      = shift_radius * std::cos(phi);
    const G4double shift_y      = shift_radius * std::sin(phi);
    const G4double top_shear    = dy2 * std::tan(alpha);

    std::vector<G4ThreeVector> vertices;
    vertices.reserve(8);
    for (const auto& point :
         std::array<G4TwoVector, 4>{G4TwoVector(-dx1, -dy1), G4TwoVector(dx1, -dy1),
                                    G4TwoVector(dx2, dy1), G4TwoVector(-dx2, dy1)}) {
      const G4TwoVector rotated = rotate(point.x(), point.y(), -0.5 * phi_twist);
      vertices.emplace_back(rotated.x(), rotated.y(), -dz);
    }
    for (const auto& point : std::array<G4TwoVector, 4>{
             G4TwoVector(-dx3 - top_shear, -dy2), G4TwoVector(dx3 - top_shear, -dy2),
             G4TwoVector(dx4 + top_shear, dy2), G4TwoVector(-dx4 + top_shear, dy2)}) {
      const G4TwoVector rotated = rotate(point.x(), point.y(), 0.5 * phi_twist);
      vertices.emplace_back(rotated.x() + shift_x, rotated.y() + shift_y, dz);
    }

    G4ThreeVector center;
    for (const auto& vertex : vertices) {
      center += vertex;
    }
    center /= static_cast<double>(vertices.size());

    auto solid          = std::make_unique<G4TessellatedSolid>(name);
    const auto add_quad = [&](const int a, const int b, const int c, const int d) {
      AddOrientedTriangle(solid.get(), center, vertices[a], vertices[b], vertices[c]);
      AddOrientedTriangle(solid.get(), center, vertices[a], vertices[c], vertices[d]);
    };

    add_quad(0, 1, 2, 3);
    add_quad(4, 5, 6, 7);
    add_quad(0, 1, 5, 4);
    add_quad(1, 2, 6, 5);
    add_quad(2, 3, 7, 6);
    add_quad(3, 0, 4, 7);
    solid->SetSolidClosed(true);
    return solid;
  }

  std::unique_ptr<G4VSolid> BuildExtrudedSolid(const std::string& name, const YAML::Node& shape,
                                               const std::string& context) {
    const std::vector<G4TwoVector> polygon =
        RequirePlanarPointList(shape, "polygon_vertices_mm", context);
    const YAML::Node sections = RequireNode(shape, "z_sections_mm", context);
    std::vector<G4ExtrudedSolid::ZSection> z_sections;
    z_sections.reserve(sections.size());
    for (std::size_t index = 0; index < sections.size(); ++index) {
      const YAML::Node section         = sections[index];
      const double z                   = RequireDouble(section, "z", context);
      const std::vector<double> offset = RequireDoubleList(section, "offset", context);
      const double scale               = RequireDouble(section, "scale", context);
      if (offset.size() != 2U) {
        throw std::runtime_error(context + ": z_sections offset must have 2 coordinates");
      }
      z_sections.emplace_back(z, G4TwoVector(offset[0], offset[1]), scale);
    }
    return std::make_unique<G4ExtrudedSolid>(name, polygon, z_sections);
  }

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// Public API implementations
// ────────────────────────────────────────────────────────────────────────────

FixtureProvenance ParseFixtureProvenance(const std::filesystem::path& path) {
  FixtureProvenance provenance;
  provenance.source_path = path;
  provenance.document    = YAML::LoadFile(path.string());
  return provenance;
}

std::string Geant4Class(const FixtureProvenance& provenance) {
  return RequireString(ShapeNode(provenance), "geant4_class",
                       provenance.source_path.string() + ".shape");
}

std::unique_ptr<G4VSolid> BuildNativeSolid(const FixtureProvenance& provenance) {
  const YAML::Node shape         = ShapeNode(provenance);
  const std::string geant4_class = Geant4Class(provenance);
  const std::string name         = provenance.source_path.stem().string() + "_native";
  const std::string context      = provenance.source_path.string() + ".shape";
  const bool has_constructor     = HasNode(shape, "constructor");
  const YAML::Node ctor = has_constructor ? RequireNode(shape, "constructor", context) : shape;

  if (geant4_class == "G4Box") {
    const std::vector<double> dimensions = RequireDoubleList(shape, "dimensions_mm", context);
    if (dimensions.size() != 3U) {
      throw std::runtime_error(context + ": dimensions_mm must have 3 entries");
    }
    return std::make_unique<G4Box>(name, 0.5 * dimensions[0], 0.5 * dimensions[1],
                                   0.5 * dimensions[2]);
  }
  if (geant4_class == "G4Cons") {
    return std::make_unique<G4Cons>(name, RequireDouble(shape, "bottom_inner_radius_mm", context),
                                    RequireDouble(shape, "bottom_outer_radius_mm", context),
                                    RequireDouble(shape, "top_inner_radius_mm", context),
                                    RequireDouble(shape, "top_outer_radius_mm", context),
                                    0.5 * RequireDouble(shape, "height_mm", context), 0.0,
                                    RequireDouble(shape, "delta_phi_deg", context) * deg);
  }
  if (geant4_class == "G4CutTubs") {
    return std::make_unique<G4CutTubs>(name, RequireDouble(ctor, "inner_radius_mm", context),
                                       RequireDouble(ctor, "outer_radius_mm", context),
                                       0.5 * RequireDouble(ctor, "height_mm", context),
                                       RequireDouble(ctor, "start_phi_deg", context) * deg,
                                       RequireDouble(ctor, "delta_phi_deg", context) * deg,
                                       RequireVector3(ctor, "low_norm", context),
                                       RequireVector3(ctor, "high_norm", context));
  }
  if (geant4_class == "G4Orb") {
    return std::make_unique<G4Orb>(name, RequireDouble(shape, "radius_mm", context));
  }
  if (geant4_class == "G4Sphere") {
    return std::make_unique<G4Sphere>(name, 0.0, RequireDouble(shape, "radius_mm", context), 0.0,
                                      360.0 * deg, 0.0, 180.0 * deg);
  }
  if (geant4_class == "G4Para") {
    return std::make_unique<G4Para>(name, RequireDouble(shape, "half_x_mm", context),
                                    RequireDouble(shape, "half_y_mm", context),
                                    RequireDouble(shape, "half_z_mm", context),
                                    RequireDouble(shape, "alpha_deg", context) * deg,
                                    RequireDouble(shape, "theta_deg", context) * deg,
                                    RequireDouble(shape, "phi_deg", context) * deg);
  }
  if (geant4_class == "G4Torus") {
    return std::make_unique<G4Torus>(name, RequireDouble(shape, "inner_tube_radius_mm", context),
                                     RequireDouble(shape, "outer_tube_radius_mm", context),
                                     RequireDouble(shape, "swept_radius_mm", context),
                                     RequireDouble(shape, "start_phi_deg", context) * deg,
                                     RequireDouble(shape, "delta_phi_deg", context) * deg);
  }
  if (geant4_class == "G4Trap") {
    return std::make_unique<G4Trap>(name, RequireDouble(shape, "half_z_mm", context),
                                    RequireDouble(shape, "theta_deg", context) * deg,
                                    RequireDouble(shape, "phi_deg", context) * deg,
                                    RequireDouble(shape, "bottom_half_y_mm", context),
                                    RequireDouble(shape, "bottom_half_x_at_minus_y_mm", context),
                                    RequireDouble(shape, "bottom_half_x_at_plus_y_mm", context),
                                    RequireDouble(shape, "bottom_alpha_deg", context) * deg,
                                    RequireDouble(shape, "top_half_y_mm", context),
                                    RequireDouble(shape, "top_half_x_at_minus_y_mm", context),
                                    RequireDouble(shape, "top_half_x_at_plus_y_mm", context),
                                    RequireDouble(shape, "top_alpha_deg", context) * deg);
  }
  if (geant4_class == "G4Trd") {
    return std::make_unique<G4Trd>(name, RequireDouble(shape, "bottom_half_x_mm", context),
                                   RequireDouble(shape, "top_half_x_mm", context),
                                   RequireDouble(shape, "bottom_half_y_mm", context),
                                   RequireDouble(shape, "top_half_y_mm", context),
                                   RequireDouble(shape, "half_z_mm", context));
  }
  if (geant4_class == "G4Tubs") {
    return std::make_unique<G4Tubs>(name, RequireDouble(shape, "inner_radius_mm", context),
                                    RequireDouble(shape, "outer_radius_mm", context),
                                    0.5 * RequireDouble(shape, "height_mm", context), 0.0,
                                    RequireDouble(shape, "delta_phi_deg", context) * deg);
  }
  if (geant4_class == "G4Ellipsoid") {
    return std::make_unique<G4Ellipsoid>(name, RequireDouble(ctor, "pxSemiAxis_mm", context),
                                         RequireDouble(ctor, "pySemiAxis_mm", context),
                                         RequireDouble(ctor, "pzSemiAxis_mm", context),
                                         RequireDouble(ctor, "zBottomCut_mm", context),
                                         RequireDouble(ctor, "zTopCut_mm", context));
  }
  if (geant4_class == "G4EllipticalCone") {
    return std::make_unique<G4EllipticalCone>(name, RequireDouble(ctor, "pxSemiAxis", context),
                                              RequireDouble(ctor, "pySemiAxis", context),
                                              RequireDouble(ctor, "zMax_mm", context),
                                              RequireDouble(ctor, "pzTopCut_mm", context));
  }
  if (geant4_class == "G4EllipticalTube") {
    return std::make_unique<G4EllipticalTube>(name, RequireDouble(ctor, "dx_mm", context),
                                              RequireDouble(ctor, "dy_mm", context),
                                              RequireDouble(ctor, "dz_mm", context));
  }
  if (geant4_class == "G4GenericPolycone") {
    const std::vector<G4TwoVector> rz_points =
        RequirePlanarPointList(ctor, "rz_points_mm", context);
    std::vector<G4double> r;
    std::vector<G4double> z;
    r.reserve(rz_points.size());
    z.reserve(rz_points.size());
    for (const auto& point : rz_points) {
      r.push_back(point.x());
      z.push_back(point.y());
    }
    return std::make_unique<G4GenericPolycone>(name,
                                               RequireDouble(ctor, "phiStart_deg", context) * deg,
                                               RequireDouble(ctor, "phiTotal_deg", context) * deg,
                                               static_cast<G4int>(r.size()), r.data(), z.data());
  }
  if (geant4_class == "G4GenericTrap") {
    std::vector<G4TwoVector> vertices = RequirePlanarPointList(ctor, "vertices_mm", context);
    if (vertices.size() != 8U) {
      throw std::runtime_error(context + ": G4GenericTrap requires 8 planar vertices");
    }
    std::vector<G4TwoVector> ordered;
    ordered.reserve(8);
    std::vector<G4TwoVector> bottom(vertices.begin(), vertices.begin() + 4);
    std::vector<G4TwoVector> top(vertices.begin() + 4, vertices.end());
    bottom = EnsureClockwise(std::move(bottom));
    top    = EnsureClockwise(std::move(top));
    ordered.insert(ordered.end(), bottom.begin(), bottom.end());
    ordered.insert(ordered.end(), top.begin(), top.end());
    return std::make_unique<G4GenericTrap>(name, RequireDouble(ctor, "halfZ_mm", context), ordered);
  }
  if (geant4_class == "G4Hype") {
    return std::make_unique<G4Hype>(name, RequireDouble(ctor, "innerRadius_mm", context),
                                    RequireDouble(ctor, "outerRadius_mm", context),
                                    RequireDouble(ctor, "innerStereo_deg", context) * deg,
                                    RequireDouble(ctor, "outerStereo_deg", context) * deg,
                                    RequireDouble(ctor, "halfLenZ_mm", context));
  }
  if (geant4_class == "G4Paraboloid") {
    return std::make_unique<G4Paraboloid>(name, RequireDouble(ctor, "dz_mm", context),
                                          RequireDouble(ctor, "r1_mm", context),
                                          RequireDouble(ctor, "r2_mm", context));
  }
  if (geant4_class == "G4Polycone") {
    const std::vector<double> z       = RequireDoubleList(ctor, "zPlane_mm", context);
    const std::vector<double> r_inner = RequireDoubleList(ctor, "rInner_mm", context);
    const std::vector<double> r_outer = RequireDoubleList(ctor, "rOuter_mm", context);
    return std::make_unique<G4Polycone>(name, RequireDouble(ctor, "phiStart_deg", context) * deg,
                                        RequireDouble(ctor, "phiTotal_deg", context) * deg,
                                        static_cast<int>(z.size()), z.data(), r_inner.data(),
                                        r_outer.data());
  }
  if (geant4_class == "G4Polyhedra") {
    const std::vector<double> z       = RequireDoubleList(ctor, "zPlane_mm", context);
    const std::vector<double> r_inner = RequireDoubleList(ctor, "rInner_mm", context);
    const std::vector<double> r_outer = RequireDoubleList(ctor, "rOuter_mm", context);
    return std::make_unique<G4Polyhedra>(name, RequireDouble(ctor, "phiStart_deg", context) * deg,
                                         RequireDouble(ctor, "phiTotal_deg", context) * deg,
                                         static_cast<int>(RequireDouble(ctor, "numSides", context)),
                                         static_cast<int>(z.size()), z.data(), r_inner.data(),
                                         r_outer.data());
  }
  if (geant4_class == "G4Tet") {
    const std::vector<G4ThreeVector> vertices = RequirePointList(shape, "vertices_mm", context);
    return std::make_unique<G4Tet>(name, vertices.at(0), vertices.at(1), vertices.at(2),
                                   vertices.at(3));
  }
  if (geant4_class == "G4TessellatedSolid") {
    return BuildTessellatedSolid(name, shape, context);
  }
  if (geant4_class == "G4ExtrudedSolid") {
    return BuildExtrudedSolid(name, shape, context);
  }
  if (geant4_class == "G4TwistedBox") {
    return std::make_unique<G4TwistedBox>(name, RequireDouble(ctor, "phi_twist_deg", context) * deg,
                                          RequireDouble(ctor, "dx_mm", context),
                                          RequireDouble(ctor, "dy_mm", context),
                                          RequireDouble(ctor, "dz_mm", context));
  }
  if (geant4_class == "G4TwistedTrap") {
    return std::make_unique<G4TwistedTrap>(
        name, RequireDouble(ctor, "phi_twist_deg", context) * deg,
        RequireDouble(ctor, "dx1_mm", context), RequireDouble(ctor, "dx2_mm", context),
        RequireDouble(ctor, "dy_mm", context), RequireDouble(ctor, "dz_mm", context));
  }
  if (geant4_class == "G4TwistedTrd") {
    return std::make_unique<G4TwistedTrd>(
        name, RequireDouble(ctor, "dx1_mm", context), RequireDouble(ctor, "dx2_mm", context),
        RequireDouble(ctor, "dy1_mm", context), RequireDouble(ctor, "dy2_mm", context),
        RequireDouble(ctor, "dz_mm", context), RequireDouble(ctor, "phi_twist_deg", context) * deg);
  }
  if (geant4_class == "G4TwistedTubs") {
    return std::make_unique<G4TwistedTubs>(
        name, RequireDouble(ctor, "phi_twist_deg", context) * deg,
        RequireDouble(ctor, "end_inner_rad_mm", context),
        RequireDouble(ctor, "end_outer_rad_mm", context), RequireDouble(ctor, "half_z_mm", context),
        RequireDouble(ctor, "dphi_deg", context) * deg);
  }
  if (geant4_class == "G4VTwistedFaceted") {
    return BuildVTwistedFacetedFallback(name, ctor, context);
  }
  if (geant4_class == "G4DisplacedSolid") {
    const std::vector<double> base        = RequireDoubleList(shape, "base_box_mm", context);
    const std::vector<double> translation = RequireDoubleList(shape, "translation_mm", context);
    if (base.size() != 3U || translation.size() != 3U) {
      throw std::runtime_error(context + ": displaced-solid metadata must be 3-vectors");
    }
    auto* base_box = new G4Box(name + "_base", 0.5 * base[0], 0.5 * base[1], 0.5 * base[2]);
    return std::make_unique<G4DisplacedSolid>(
        name, base_box, nullptr, G4ThreeVector(translation[0], translation[1], translation[2]));
  }
  if (geant4_class == "G4ScaledSolid") {
    const double radius               = RequireDouble(shape, "base_radius_mm", context);
    const std::vector<double> factors = RequireDoubleList(shape, "scale_factors", context);
    if (factors.size() != 3U) {
      throw std::runtime_error(context + ": scale_factors must have 3 values");
    }
    auto* base_sphere = new G4Orb(name + "_base", radius);
    return std::make_unique<G4ScaledSolid>(name, base_sphere,
                                           G4Scale3D(factors[0], factors[1], factors[2]));
  }
  if (geant4_class == "G4UnionSolid" || geant4_class == "G4IntersectionSolid" ||
      geant4_class == "G4SubtractionSolid") {
    return BuildBooleanBoxSolid(geant4_class, name, shape, context);
  }
  if (geant4_class == "G4MultiUnion") {
    return BuildMultiUnionSolid(name, shape, context);
  }

  throw std::runtime_error("Unsupported Geant4 fixture class in " + context + ": " + geant4_class);
}

TopoDS_Shape LoadImportedShape(const FixtureValidationRequest& request) {
  const auto step_path = ResolveFixtureStepPath(request.manifest, request.fixture);
  STEPControl_Reader reader;
  const IFSelect_ReturnStatus read_status = reader.ReadFile(step_path.string().c_str());
  if (read_status != IFSelect_RetDone) {
    throw std::runtime_error("STEPControl_Reader failed to read " + step_path.string());
  }
  if (reader.TransferRoots() <= 0) {
    throw std::runtime_error("STEPControl_Reader transferred no roots for " + step_path.string());
  }
  const TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) {
    throw std::runtime_error("Transferred STEP shape is null for " + step_path.string());
  }
  return shape;
}

std::unique_ptr<G4VSolid> BuildNativeSolidForRequest(const FixtureValidationRequest& request,
                                                     const FixtureProvenance& provenance) {
  if (Geant4Class(provenance) == "G4OCCTSolid") {
    return std::make_unique<G4OCCTSolid>(request.fixture.id + "_native",
                                         LoadImportedShape(request));
  }
  return BuildNativeSolid(provenance);
}

G4ThreeVector BoundingBoxCenter(const G4VSolid& solid) {
  G4ThreeVector minimum;
  G4ThreeVector maximum;
  solid.BoundingLimits(minimum, maximum);
  return 0.5 * (minimum + maximum);
}

G4ThreeVector FixtureComparisonOrigin(const FixtureProvenance& provenance, const G4VSolid& solid) {
  const YAML::Node shape         = ShapeNode(provenance);
  const std::string geant4_class = Geant4Class(provenance);

  // G4OCCTSolid fixtures have no generator YAML block; use the bounding-box centre.
  if (geant4_class == "G4OCCTSolid") {
    return BoundingBoxCenter(solid);
  }

  if (geant4_class == "G4Tet") {
    const std::vector<G4ThreeVector> vertices =
        RequirePointList(shape, "vertices_mm", provenance.source_path.string());
    G4ThreeVector centroid;
    for (const auto& vertex : vertices) {
      centroid += vertex;
    }
    return centroid / static_cast<double>(vertices.size());
  }

  if (geant4_class == "G4ScaledSolid" || geant4_class == "G4Ellipsoid" ||
      geant4_class == "G4EllipticalCone" || geant4_class == "G4EllipticalTube") {
    return G4ThreeVector();
  }

  const YAML::Node generator =
      RequireNode(provenance.document, "generator", provenance.source_path.string());
  if (generator["tool"].IsDefined() &&
      generator["tool"].as<std::string>() == "generate_twisted_fixtures") {
    return G4ThreeVector();
  }

  return BoundingBoxCenter(solid);
}

std::vector<G4ThreeVector> GenerateDirections(const std::size_t count) {
  std::vector<G4ThreeVector> directions;
  directions.reserve(count);
  const std::array<G4ThreeVector, 6> cardinal = {
      G4ThreeVector(1.0, 0.0, 0.0),  G4ThreeVector(-1.0, 0.0, 0.0), G4ThreeVector(0.0, 1.0, 0.0),
      G4ThreeVector(0.0, -1.0, 0.0), G4ThreeVector(0.0, 0.0, 1.0),  G4ThreeVector(0.0, 0.0, -1.0)};
  for (std::size_t index = 0; index < count && index < cardinal.size(); ++index) {
    directions.push_back(cardinal[index]);
  }
  for (std::size_t index = directions.size(); index < count; ++index) {
    const double k = static_cast<double>(index - cardinal.size()) + 0.5;
    const double z =
        1.0 - 2.0 * k / static_cast<double>(std::max<std::size_t>(1, count - cardinal.size()));
    const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double phi    = kGoldenAngle * static_cast<double>(index);
    directions.emplace_back(radius * std::cos(phi), radius * std::sin(phi), z);
    directions.back() = directions.back().unit();
  }
  return directions;
}

namespace {
  /// Prime bases for the 3-D Halton sequence; coprime bases give low discrepancy.
  constexpr std::size_t kHaltonBaseX = 2U;
  constexpr std::size_t kHaltonBaseY = 3U;
  constexpr std::size_t kHaltonBaseZ = 5U;

  /// Compute the i-th term of the Halton sequence in the given base.
  double Halton(std::size_t index, std::size_t base) {
    double result   = 0.0;
    double fraction = 1.0;
    while (index > 0U) {
      fraction /= static_cast<double>(base);
      result += fraction * static_cast<double>(index % base);
      index /= base;
    }
    return result;
  }
} // namespace

std::vector<G4ThreeVector> GenerateBoundingBoxPoints(const G4VSolid& solid,
                                                     const std::size_t count) {
  G4ThreeVector bb_min;
  G4ThreeVector bb_max;
  solid.BoundingLimits(bb_min, bb_max);
  const G4ThreeVector extents = bb_max - bb_min;

  std::vector<G4ThreeVector> points;
  points.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t i = index + 1U; // Halton is 1-indexed conventionally
    const double x      = bb_min.x() + Halton(i, kHaltonBaseX) * extents.x();
    const double y      = bb_min.y() + Halton(i, kHaltonBaseY) * extents.y();
    const double z      = bb_min.z() + Halton(i, kHaltonBaseZ) * extents.z();
    points.emplace_back(x, y, z);
  }
  return points;
}

} // namespace g4occt::tests::geometry
