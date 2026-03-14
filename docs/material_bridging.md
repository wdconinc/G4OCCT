# G4OCCT — Material Bridging: Strategies for Mapping OCCT/STEP Materials to Geant4

## 1. The Problem

Geant4 simulations require detailed material descriptions for each detector
volume.  A `G4Material` encodes:

* Chemical composition (elements and their fractional masses/mole-fractions).
* Density (g/cm³).
* Temperature and pressure (for gases).
* State (solid, liquid, gas).
* Derived quantities (radiation length X₀, nuclear interaction length λᵢ,
  mean excitation energy I, …) computed by Geant4 from the above.

STEP files (and OCCT geometry descriptions in general) carry *engineering*
material data:

* A human-readable name (e.g. `"AISI 316L Steel"`, `"Aluminium 6061-T6"`).
* Optional density.
* Optional Young's modulus, Poisson ratio, yield strength (mechanical).
* Optional colour / visual attributes (via XDE).

The physics information required by Geant4 is typically *absent* from STEP
files.  Bridging this gap is therefore a non-trivial step in any CAD-to-Geant4
geometry workflow.

---

## 2. Strategies

### 2.1 Name-Based Lookup (Recommended for Initial Implementation)

**Idea:** Map the STEP/OCCT material name to a `G4Material` from the Geant4
NIST material database (`G4NistManager`).

**Workflow:**
1. Parse the STEP file; extract the material name from the XDE label
   (`XCAFDoc_MaterialTool`).
2. Normalise the name (strip whitespace, convert to uppercase).
3. Look up a matching entry in a configurable name → Geant4-material map.
4. Fall back to a default material (e.g. `G4_AIR` or `G4_Fe`) if no match
   is found.

**Pros:**
* Simple, zero external dependencies.
* The NIST database covers ~300 predefined materials.

**Cons:**
* Relies on consistent naming conventions in STEP files (not guaranteed).
* Engineering alloy names rarely match NIST names exactly.

**Implementation sketch:**
```cpp
G4Material* LookupMaterial(const std::string& stepName) {
  static const std::map<std::string, std::string> nameMap = {
    {"STAINLESS STEEL",  "G4_STAINLESS-STEEL"},
    {"ALUMINIUM",        "G4_Al"},
    {"COPPER",           "G4_Cu"},
    {"LEAD",             "G4_Pb"},
    {"SILICON",          "G4_Si"},
    {"CARBON FIBRE",     "G4_C"},
    {"AIR",              "G4_AIR"},
    {"VACUUM",           "G4_Galactic"},
  };
  auto it = nameMap.find(ToUpper(Trim(stepName)));
  if (it != nameMap.end())
    return G4NistManager::Instance()->FindOrBuildMaterial(it->second);
  return G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR"); // fallback
}
```

---

### 2.2 Density-Based Heuristic

**Idea:** Use the density value from the STEP file to select the closest
matching Geant4 material.

**Workflow:**
1. Extract density ρ from the OCCT XDE material attribute.
2. Query the NIST database for all materials with |ρ_NIST − ρ_STEP| / ρ_STEP
   below a threshold (e.g. 5 %).
3. Among candidates, prefer materials whose name partially matches the STEP
   material name.

**Pros:**
* Density is a robust, unit-bearing scalar — less sensitive to naming
  conventions.

**Cons:**
* Many materials share similar densities (e.g. several steels, copper alloys).
* No chemical composition information is inferred.

---

### 2.3 External Material Database / Configuration File

**Idea:** Provide an external YAML/JSON/XML configuration file that the user
populates to map STEP material names to `G4Material` definitions.

**Format example (YAML):**
```yaml
materials:
  - step_name: "AISI 316L"
    geant4_name: "G4_STAINLESS-STEEL"

  - step_name: "Al 6061"
    geant4_name: "G4_Al"

  - step_name: "FR4"
    density: 1.86  # g/cm3
    components:
      - element: Si
        fraction: 0.18
      - element: O
        fraction: 0.39
      - element: C
        fraction: 0.28
      - element: H
        fraction: 0.03
      - element: Br
        fraction: 0.12

  - step_name: "*"   # catch-all default
    geant4_name: "G4_AIR"
```

**Pros:**
* User-controlled, flexible.
* Allows custom multi-element materials not in NIST.
* Separates material mapping from code — no recompilation needed.

**Cons:**
* Requires the user to create and maintain the configuration file.
* Parser must be implemented (or a library dependency added, e.g. yaml-cpp).

---

### 2.4 Inline Annotations in the CAD Model

**Idea:** Encode Geant4 material names directly in the STEP file using
user-defined attributes or the `PRODUCT_DEFINITION_CONTEXT` entity name field.

**Workflow:**
1. In the CAD tool, set the material name of each part to the exact Geant4
   NIST material string (e.g. `"G4_STAINLESS-STEEL"`).
2. G4OCCT reads the name from the XDE label and passes it directly to
   `G4NistManager::FindOrBuildMaterial`.

**Pros:**
* Zero ambiguity — the material identity is explicit in the file.
* No external configuration needed.

**Cons:**
* Requires CAD tool discipline and user training.
* STEP files edited by automated tools may overwrite custom names.

---

### 2.5 Geant4 GDML Overlay

**Idea:** Build the physical geometry from the STEP file, then read an
accompanying GDML file that provides material assignments by volume name.

**Workflow:**
1. Import the STEP file → create G4OCCT geometry (solids + placements).
2. Parse a GDML file containing `<physvol>` material attributes.
3. Match GDML `name` attributes to the OCCT XDE label names.
4. Assign `G4Material*` to each `G4LogicalVolume`.

**Pros:**
* GDML is the standard Geant4 geometry description language.
* Leverages existing GDML tooling and validation.

**Cons:**
* Requires maintaining two separate files (STEP + GDML) in sync.
* GDML volume naming must match XDE label naming exactly.

---

## 3. Recommended Phased Approach

| Phase | Strategy | Goal |
|---|---|---|
| v0.1 | Hard-coded name map (2.1) | First working end-to-end example |
| v0.2 | External config file (2.3) | User-friendly, no recompilation |
| v0.3 | Density heuristic as fallback (2.2) | Reduce unmapped-material rate |
| v1.0 | GDML overlay support (2.5) | Full integration with GDML ecosystem |

---

## 4. Open Questions

1. **Unit consistency:** STEP files use metres or millimetres depending on
   the AP version and CAD tool.  G4OCCT must confirm and convert units for
   both geometry and material density.

2. **Composite materials:** Engineering materials such as printed circuit
   board laminates (FR4), carbon-fibre-reinforced polymers (CFRP), and
   aerogels have no direct NIST equivalent and require custom
   `G4Material::CreateMixture` definitions.

3. **Optical properties:** Geant4 optical physics (`G4OpticalSurface`,
   `G4MaterialPropertiesTable`) has no STEP equivalent at all.  These must
   always be specified externally (e.g. via GDML or user code).

4. **Material de-duplication:** When the same STEP material name appears on
   multiple volumes, G4OCCT should ensure a single `G4Material*` is shared
   rather than creating duplicate objects.

5. **Unknown materials:** A clear user-facing warning (or configurable error)
   should be emitted when a STEP material name cannot be resolved.  Silently
   using air as a fallback can produce incorrect physics results.
