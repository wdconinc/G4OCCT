// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTAssemblyVolume.cc
/// @brief Implementation of G4OCCTAssemblyVolume.

#include "G4OCCT/G4OCCTAssemblyVolume.hh"

#include "G4OCCT/G4OCCTSolid.hh"

// OCCT BRep / geometry
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

// OCCT XDE
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_Location.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

// Geant4
#include <G4Exception.hh>
#include <G4RotationMatrix.hh>
#include <G4ThreeVector.hh>

// CLHEP
#include <CLHEP/Vector/RotationInterfaces.h>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

/// Convert an OCCT @p trsf to a Geant4 rotation + translation pair.
/// STEP/OCCT and Geant4 both use millimetres; no unit conversion is needed.
std::pair<G4RotationMatrix, G4ThreeVector> TrsfToG4(const gp_Trsf& trsf) {
  // CLHEP::HepRotation's 9-argument (row-major) constructor is protected.
  // Use the public HepRep3x3 constructor instead.
  G4RotationMatrix rot(CLHEP::HepRep3x3(trsf.Value(1, 1), trsf.Value(1, 2), trsf.Value(1, 3),
                                        trsf.Value(2, 1), trsf.Value(2, 2), trsf.Value(2, 3),
                                        trsf.Value(3, 1), trsf.Value(3, 2), trsf.Value(3, 3)));
  G4ThreeVector trans(trsf.Value(1, 4), trsf.Value(2, 4), trsf.Value(3, 4));
  return {rot, trans};
}

/// Compose a TopLoc_Location into a gp_Trsf by walking the datum-power chain.
///
/// This follows the approach documented in docs/step_assembly_import.md §4.3.
/// For a simple (non-compound) location with power ±1 the loop runs exactly
/// once.  Compound locations with power > 1 are correctly handled by repeated
/// application of the datum transformation.
gp_Trsf LocationToTrsf(const TopLoc_Location& loc) {
  gp_Trsf result; // identity by default
  for (TopLoc_Location cursor = loc; !cursor.IsIdentity(); cursor = cursor.NextLocation()) {
    gp_Trsf datum          = cursor.FirstDatum()->Trsf();
    Standard_Integer power = cursor.FirstPower();
    if (power < 0) {
      datum.Invert();
      power = -power;
    }
    gp_Trsf step; // identity
    for (Standard_Integer k = 0; k < power; ++k) {
      step.Multiply(datum);
    }
    result.Multiply(step);
  }
  return result;
}

/// Retrieve the XDE name attribute for @p label, or return an empty string.
G4String GetLabelName(const TDF_Label& label) {
  Handle(TDataStd_Name) nameAttr;
  if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
    TCollection_AsciiString ascii(nameAttr->Get());
    return G4String(ascii.ToCString());
  }
  return G4String{};
}

/// Read the material name attached to @p label via the XDE material tool.
/// Returns an empty string when no material attribute is found.
G4String GetMaterialName(const TDF_Label& label, const Handle(XCAFDoc_MaterialTool) & matTool) {
  Handle(TCollection_HAsciiString) matName;
  Handle(TCollection_HAsciiString) matDescription;
  Standard_Real density = 0.0;
  Handle(TCollection_HAsciiString) densityName;
  Handle(TCollection_HAsciiString) densityValType;
  if (matTool->GetMaterial(label, matName, matDescription, density, densityName, densityValType)) {
    if (!matName.IsNull()) {
      return G4String(matName->ToCString());
    }
  }
  return G4String{};
}

/// Recenter @p shape by moving its bounding-box centroid to the OCCT origin
/// using Strategy C from docs/reference_position.md (shape.Moved(), no BRep copy).
///
/// On return @p centroid holds the location of the centroid in the original
/// shape frame (i.e. the opposite of the applied recentering translation).
/// Callers must compose this offset into the placement transformation:
///   T_eff = composedTrsf * Translate(centroid)
TopoDS_Shape RecenterShape(const TopoDS_Shape& shape, gp_Vec& centroid) {
  Bnd_Box bbox;
  BRepBndLib::AddOptimal(shape, bbox, Standard_False);
  if (bbox.IsVoid()) {
    centroid = gp_Vec(0.0, 0.0, 0.0);
    return shape;
  }
  Standard_Real xMin = 0.0, yMin = 0.0, zMin = 0.0;
  Standard_Real xMax = 0.0, yMax = 0.0, zMax = 0.0;
  bbox.Get(xMin, yMin, zMin, xMax, yMax, zMax);

  centroid = gp_Vec(0.5 * (xMin + xMax), 0.5 * (yMin + yMax), 0.5 * (zMin + zMax));

  gp_Trsf centerTrsf;
  centerTrsf.SetTranslation(gp_Vec(-centroid.X(), -centroid.Y(), -centroid.Z()));
  return shape.Moved(TopLoc_Location(centerTrsf));
}

/// Make a name unique within @p usedNames by appending `_1`, `_2`, … as needed.
///
/// On the first call with a given @p name the name is returned unchanged.
/// On subsequent calls a numeric suffix is appended to avoid duplicates.
G4String MakeUniqueName(const G4String& name, std::map<G4String, int>& usedNames) {
  auto [it, inserted] = usedNames.emplace(name, 0);
  if (inserted) {
    return name;
  }
  // Generate a unique candidate by incrementing the per-base counter.
  G4String candidate;
  do {
    ++(it->second);
    candidate = name + "_" + std::to_string(it->second);
  } while (usedNames.count(candidate) > 0);
  usedNames.emplace(candidate, 0);
  return candidate;
}

/// Return a stable string key for @p label suitable for use in a std::map.
///
/// `TDF_Label` does not define `operator<`, so it cannot be used directly as a
/// std::map key.  The colon-separated tag path (e.g. "0:1:1:2") is unique
/// within a document and provides a stable ordering.
std::string LabelKey(const TDF_Label& label) {
  TCollection_AsciiString entry;
  TDF_Tool::Entry(label, entry);
  return std::string(entry.ToCString());
}

} // namespace

// ── BuildContext ──────────────────────────────────────────────────────────────

/// State threaded through the recursive XDE label traversal.
struct G4OCCTAssemblyVolume::BuildContext {
  /// OCCT shape tool for the XDE document.
  Handle(XCAFDoc_ShapeTool) shapeTool;
  /// OCCT material tool for the XDE document.
  Handle(XCAFDoc_MaterialTool) matTool;
  /// User-supplied material map.
  const G4OCCTMaterialMap& materialMap;

  /// Prototype map: XDE label entry string → (logical volume, centroid in original shape frame).
  /// The centroid is stored alongside the logical volume so that repeated instances can compose
  /// the correct recentering offset without recomputing the bounding box.
  std::map<std::string, std::pair<G4OCCTLogicalVolume*, gp_Vec>> prototypeMap;
  /// Names already used by logical volumes; used to disambiguate duplicates.
  std::map<G4String, int> usedNames;
  /// Flat collection of all created logical volumes (output to caller).
  std::map<G4String, G4OCCTLogicalVolume*>* logicalVolumes{nullptr};
};

// ── Recursive import ──────────────────────────────────────────────────────────

void G4OCCTAssemblyVolume::ImportLabel(const TDF_Label& label, G4AssemblyVolume* parentAssembly,
                                       const gp_Trsf& composedTrsf, BuildContext& ctx) {
  const Handle(XCAFDoc_ShapeTool) & shapeTool = ctx.shapeTool;

  // ── Assembly label: recurse into components ─────────────────────────────────
  if (shapeTool->IsAssembly(label)) {
    TDF_LabelSequence components;
    shapeTool->GetComponents(label, components, /*recursive=*/Standard_False);

    for (Standard_Integer i = 1; i <= components.Length(); ++i) {
      const TDF_Label& comp = components.Value(i); // always a reference label

      // Extract the component's placement in the parent frame.
      // FindAttribute returns false if no location is stored; in that case
      // compLoc remains default-constructed (identity).
      TopLoc_Location compLoc;
      Handle(XCAFDoc_Location) locAttr;
      if (comp.FindAttribute(XCAFDoc_Location::GetID(), locAttr)) {
        compLoc = locAttr->Get();
      }
      gp_Trsf compTrsf = LocationToTrsf(compLoc);

      // Compose: first apply component's local placement, then the parent's.
      gp_Trsf childTrsf = composedTrsf;
      childTrsf.Multiply(compTrsf);

      // Resolve reference to the actual shape label, then recurse.
      TDF_Label referred;
      if (shapeTool->GetReferredShape(comp, referred)) {
        ImportLabel(referred, parentAssembly, childTrsf, ctx);
      } else {
        G4Exception("G4OCCTAssemblyVolume::ImportLabel", "G4OCCT_Asm001", JustWarning,
                    "Assembly component has no referred shape; skipping.");
      }
    }
    return;
  }

  // ── Simple shape label: create or reuse a logical volume ────────────────────
  if (shapeTool->IsSimpleShape(label)) {
    const std::string labelKey = LabelKey(label);

    G4OCCTLogicalVolume* lv = nullptr;
    auto protoIt            = ctx.prototypeMap.find(labelKey);
    gp_Vec centroid;

    if (protoIt != ctx.prototypeMap.end()) {
      // Reuse the existing logical volume — instance sharing.
      // Retrieve the cached centroid to avoid recomputing the bounding box.
      lv       = protoIt->second.first;
      centroid = protoIt->second.second;
    } else {
      // First encounter: build solid + logical volume.
      TopoDS_Shape rawShape = shapeTool->GetShape(label);
      if (rawShape.IsNull()) {
        G4Exception("G4OCCTAssemblyVolume::ImportLabel", "G4OCCT_Asm002", JustWarning,
                    "Simple-shape label has a null OCCT shape; skipping.");
        return;
      }

      // Resolve material by XDE material attribute, falling back to the part
      // (label) name when no material attribute is present in the STEP file.
      // This accommodates STEP writers that do not write material attributes.
      G4String matKey = GetMaterialName(label, ctx.matTool);
      if (matKey.empty()) {
        matKey = GetLabelName(label);
      }
      if (matKey.empty()) {
        G4Exception("G4OCCTAssemblyVolume::ImportLabel", "G4OCCT_Asm003", FatalException,
                    "Simple-shape label carries neither a STEP material attribute nor a part name. "
                    "All shapes must have materials registered in G4OCCTMaterialMap.");
        return; // unreachable; silences compiler warning
      }
      G4Material* material = ctx.materialMap.Resolve(matKey);

      // Determine unique part name.
      G4String rawName = GetLabelName(label);
      if (rawName.empty()) {
        rawName = "Part";
      }
      G4String uniqueName = MakeUniqueName(rawName, ctx.usedNames);

      // Recenter the shape per docs/reference_position.md Strategy C.
      // The centroid vector records the old centroid position in the original
      // shape frame; it is composed into the placement below.
      TopoDS_Shape centeredShape = RecenterShape(rawShape, centroid);

      auto* solid = new G4OCCTSolid(uniqueName + "_solid", centeredShape);
      lv          = new G4OCCTLogicalVolume(solid, material, uniqueName, centeredShape);

      // Cache: store both the logical volume and its centroid so that repeated
      // instances can reuse the centroid without recomputing the bounding box.
      ctx.prototypeMap.emplace(labelKey, std::make_pair(lv, centroid));
      if (ctx.logicalVolumes) {
        (*ctx.logicalVolumes)[uniqueName] = lv;
      }
    }

    // Absorb the recentering offset into composedTrsf so that the solid's
    // local origin (now the centroid) is placed at the correct world position.
    //
    // After recentering, a point at the solid's local origin corresponds to
    // the centroid in the original frame.  The effective placement transform
    // must first shift by `centroid` (from recentered → original local frame)
    // and then apply `composedTrsf` (original local → world):
    //   T_eff = composedTrsf * Translate(centroid)
    gp_Trsf recenterComp;
    recenterComp.SetTranslation(centroid);
    const gp_Trsf effectiveTrsf = composedTrsf.Multiplied(recenterComp);

    auto [rot, trans] = TrsfToG4(effectiveTrsf);
    parentAssembly->AddPlacedVolume(lv, trans, new G4RotationMatrix(rot));
    return;
  }

  // Unrecognised label category (e.g. free-shape reference) — skip silently.
}

// ── Factory ───────────────────────────────────────────────────────────────────

G4OCCTAssemblyVolume* G4OCCTAssemblyVolume::FromSTEP(const std::string& path,
                                                     const G4OCCTMaterialMap& materialMap) {
  // ── Open XDE document and read the STEP file ─────────────────────────────────
  Handle(TDocStd_Application) app = new TDocStd_Application;
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-CAF", doc);

  STEPCAFControl_Reader cafReader;
  cafReader.SetNameMode(Standard_True);
  cafReader.SetMatMode(Standard_True);
  cafReader.SetColorMode(Standard_True);

  if (cafReader.ReadFile(path.c_str()) != IFSelect_RetDone) {
    throw std::runtime_error("G4OCCTAssemblyVolume::FromSTEP: failed to read STEP file: " + path);
  }
  if (!cafReader.Transfer(doc)) {
    throw std::runtime_error("G4OCCTAssemblyVolume::FromSTEP: failed to transfer STEP document: " +
                             path);
  }

  Handle(XCAFDoc_ShapeTool) shapeTool  = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_MaterialTool) matTool = XCAFDoc_DocumentTool::MaterialTool(doc->Main());

  // ── Collect top-level (free) shape labels ────────────────────────────────────
  TDF_LabelSequence freeShapes;
  shapeTool->GetFreeShapes(freeShapes);
  if (freeShapes.IsEmpty()) {
    throw std::runtime_error(
        "G4OCCTAssemblyVolume::FromSTEP: STEP file contains no top-level shapes: " + path);
  }

  // ── Build the Geant4 volume hierarchy ────────────────────────────────────────
  auto* result = new G4OCCTAssemblyVolume;

  BuildContext ctx{
      .shapeTool      = shapeTool,
      .matTool        = matTool,
      .materialMap    = materialMap,
      .logicalVolumes = &result->fLogicalVolumes,
  };

  const gp_Trsf identity; // starts as the identity transformation
  for (Standard_Integer i = 1; i <= freeShapes.Length(); ++i) {
    ImportLabel(freeShapes.Value(i), result, identity, ctx);
  }

  return result;
}
