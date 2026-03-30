// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSensitiveDetectorMap.hh
/// @brief Declaration of G4OCCTSensitiveDetectorMap.

#ifndef G4OCCT_G4OCCTSensitiveDetectorMap_hh
#define G4OCCT_G4OCCTSensitiveDetectorMap_hh

#include <G4String.hh>

#include <cstddef>
#include <utility>
#include <vector>

class G4VSensitiveDetector;

/**
 * @brief Maps volume name patterns to Geant4 G4VSensitiveDetector objects.
 *
 * Provides an ordered lookup table from volume name patterns to the
 * corresponding Geant4 `G4VSensitiveDetector` pointers.  Two matching
 * strategies are supported (checked in insertion order; first match wins):
 *
 * 1. **Exact match** — `volumeName == pattern`
 * 2. **Prefix match** — `volumeName` starts with `pattern + "_"` and the
 *    remaining suffix consists entirely of decimal digits.  This handles
 *    Geant4's `MakeUniqueName` deduplication convention (e.g. `"Absorber_1"`,
 *    `"Absorber_2"` are both matched by the pattern `"Absorber"`).
 *
 * Most volumes in a detector are not sensitive, so `Resolve()` returns
 * `nullptr` for unmatched names rather than throwing a fatal error.
 *
 * ### Usage
 * ```cpp
 * G4OCCTSensitiveDetectorMap sdMap;
 * sdMap.Add("Absorber", absorberSD);
 * sdMap.Add("Gap",      gapSD);
 *
 * G4VSensitiveDetector* sd = sdMap.Resolve("Absorber_1");  // returns absorberSD
 * G4VSensitiveDetector* no = sdMap.Resolve("World");       // returns nullptr
 * ```
 */
class G4OCCTSensitiveDetectorMap {
public:
  G4OCCTSensitiveDetectorMap() = default;

  /**
   * Register a mapping from a volume name pattern to a sensitive detector.
   *
   * If @p pattern is already registered the previous entry is silently
   * overwritten.
   *
   * @param pattern Volume name pattern (case-sensitive).  Used for both exact
   *                and prefix matching (see class documentation).
   * @param sd      Non-null pointer to the sensitive detector.  A null pointer
   *                triggers a fatal `G4Exception` with code `G4OCCT_SDMap000`.
   */
  void Add(const G4String& pattern, G4VSensitiveDetector* sd);

  /**
   * Look up the sensitive detector for a given volume name.
   *
   * Checks each registered entry in insertion order and returns the first
   * match (exact or prefix).  Returns `nullptr` if no entry matches — this
   * is the expected result for non-sensitive volumes.
   *
   * @param volumeName Geant4 logical volume name to resolve.
   * @return Pointer to the matching `G4VSensitiveDetector`, or `nullptr` if
   *         no entry matches.
   */
  G4VSensitiveDetector* Resolve(const G4String& volumeName) const;

  /**
   * Return the number of registered entries.
   */
  std::size_t Size() const { return fEntries.size(); }

private:
  std::vector<std::pair<G4String, G4VSensitiveDetector*>> fEntries;
};

#endif // G4OCCT_G4OCCTSensitiveDetectorMap_hh
