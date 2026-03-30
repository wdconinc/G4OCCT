// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSensitiveDetectorMapReader.hh
/// @brief Declaration of G4OCCTSensitiveDetectorMapReader.

#ifndef G4OCCT_G4OCCTSensitiveDetectorMapReader_hh
#define G4OCCT_G4OCCTSensitiveDetectorMapReader_hh

#include "G4OCCT/G4OCCTSensitiveDetectorMap.hh"

#include <G4String.hh>

/**
 * @brief Parses an XML sensitive-detector-map file into a G4OCCTSensitiveDetectorMap.
 *
 * Reads a simple XML file and resolves sensitive detector names via
 * `G4SDManager`.  **This reader must be called after all sensitive detectors
 * have been registered in `G4SDManager`.**
 *
 * ## File format
 *
 * The root element must be `<sensitive_detector_map>`.  Each child
 * `<volume>` element carries two required attributes:
 *
 * - `name`    — volume name pattern used for matching (see
 *               `G4OCCTSensitiveDetectorMap` for matching rules).
 * - `sensDet` — the sensitive detector name as registered in `G4SDManager`
 *               (i.e. the string passed to
 *               `G4SDManager::AddNewDetector()`).
 *
 * ```xml
 * <sensitive_detector_map>
 *   <volume name="Absorber" sensDet="AbsorberSD"/>
 *   <volume name="Gap"      sensDet="GapSD"/>
 * </sensitive_detector_map>
 * ```
 *
 * ## Error codes
 *
 * | Code                  | Condition                                      |
 * |-----------------------|------------------------------------------------|
 * | `G4OCCT_SDReader000`  | `<volume>` element missing `name` attribute    |
 * | `G4OCCT_SDReader001`  | `<volume>` element missing `sensDet` attribute |
 * | `G4OCCT_SDReader002`  | SD name not found in `G4SDManager`             |
 * | `G4OCCT_SDReader003`  | Xerces `XMLException` during parse             |
 * | `G4OCCT_SDReader004`  | Xerces `DOMException` during parse             |
 * | `G4OCCT_SDReader005`  | Null DOM document after parse                  |
 * | `G4OCCT_SDReader006`  | Null root element                              |
 * | `G4OCCT_SDReader007`  | Wrong root tag (not `sensitive_detector_map`)  |
 *
 * ## Usage
 * ```cpp
 * // After SDs are registered:
 * G4OCCTSensitiveDetectorMapReader reader;
 * G4OCCTSensitiveDetectorMap sdMap = reader.ReadFile("sd_map.xml");
 * ```
 */
class G4OCCTSensitiveDetectorMapReader {
public:
  G4OCCTSensitiveDetectorMapReader()  = default;
  ~G4OCCTSensitiveDetectorMapReader() = default;

  /**
   * Parse the sensitive-detector-map XML file at @p path and return the
   * populated map.
   *
   * Must be called **after** all sensitive detectors have been registered
   * in `G4SDManager`.
   *
   * @param path Filesystem path to the XML sensitive-detector-map file.
   * @return `G4OCCTSensitiveDetectorMap` with one entry per `<volume>` element.
   * @throws G4Exception (FatalException) on any parse or resolution error.
   */
  G4OCCTSensitiveDetectorMap ReadFile(const G4String& path);
};

#endif // G4OCCT_G4OCCTSensitiveDetectorMapReader_hh
