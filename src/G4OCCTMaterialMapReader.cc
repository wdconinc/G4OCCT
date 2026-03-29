// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTMaterialMapReader.cc
/// @brief Implementation of G4OCCTMaterialMapReader.

#include "G4OCCT/G4OCCTMaterialMapReader.hh"

// Xerces — provided transitively by Geant4::G4gdml.
// G4GDMLRead.hh already includes the Xerces DOM headers; we repeat them here
// for clarity and to document the direct dependency.
#include <xercesc/dom/DOM.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/util/PlatformUtils.hpp>

// Geant4
#include <G4Exception.hh>
#include <G4Material.hh>
#include <G4NistManager.hh>

G4OCCTMaterialMap G4OCCTMaterialMapReader::ReadFile(const G4String& path) {
  // Xerces initialization is ref-counted; safe to call multiple times.
  xercesc::XMLPlatformUtils::Initialize();

  G4OCCTMaterialMap result;

  // Pre-parse fatal errors (MatReader001–005) are saved here and reported
  // after the parser is destroyed and Terminate() has been called.  Calling
  // Terminate() inside the parser's {} scope and then returning through the
  // scope exit would invoke XercesDOMParser::~XercesDOMParser() after
  // Terminate() — a SEGFAULT if a non-aborting G4VExceptionHandler allows
  // execution to continue past G4Exception().
  G4String preFatalCode;
  G4String preFatalMsg;

  // IIFE: contains the XercesDOMParser so it is destroyed before Terminate().
  // Early 'return' from the lambda exits the lambda scope cleanly, ensuring
  // the parser destructor runs before Terminate() is called below.
  [&]() {
    xercesc::XercesDOMParser parser;
    parser.setDoNamespaces(false);
    parser.setDoSchema(false);
    parser.setCreateEntityReferenceNodes(false);

    try {
      parser.parse(path.c_str());
    } catch (const xercesc::XMLException& e) {
      preFatalCode = "G4OCCT_MatReader001";
      preFatalMsg  = "XML error parsing '" + path + "': " + Transcode(e.getMessage());
      return;
    } catch (const xercesc::DOMException& e) {
      preFatalCode = "G4OCCT_MatReader002";
      preFatalMsg  = "DOM error parsing '" + path + "': " + Transcode(e.getMessage());
      return;
    }

    const xercesc::DOMDocument* const doc = parser.getDocument();
    if (!doc) {
      preFatalCode = "G4OCCT_MatReader003";
      preFatalMsg  = "Cannot open document: " + path;
      return;
    }

    const xercesc::DOMElement* const root = doc->getDocumentElement();
    if (!root) {
      preFatalCode = "G4OCCT_MatReader004";
      preFatalMsg  = "Empty document: " + path;
      return;
    }

    const G4String rootTag = Transcode(root->getTagName());
    if (rootTag != "materials") {
      preFatalCode = "G4OCCT_MatReader005";
      preFatalMsg  = "Root element must be <materials>, got <" + rootTag + "> in '" + path + "'";
      return;
    }

    // ── Pass 1: isotopes and elements ──────────────────────────────────────
    // Must precede materials so that <fraction ref="Si"/> can resolve "Si".
    for (xercesc::DOMNode* node = root->getFirstChild(); node != nullptr;
         node                   = node->getNextSibling()) {
      if (node->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) {
        continue;
      }
      const auto* child = dynamic_cast<const xercesc::DOMElement*>(node);
      if (!child) {
        continue;
      }
      const G4String tag = Transcode(child->getTagName());
      if (tag == "isotope") {
        IsotopeRead(child);
      } else if (tag == "element") {
        ElementRead(child);
      }
    }

    // ── Pass 2: materials ──────────────────────────────────────────────────
    for (xercesc::DOMNode* node = root->getFirstChild(); node != nullptr;
         node                   = node->getNextSibling()) {
      if (node->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) {
        continue;
      }
      const auto* child = dynamic_cast<const xercesc::DOMElement*>(node);
      if (!child) {
        continue;
      }
      if (Transcode(child->getTagName()) != "material") {
        continue;
      }

      // Extract our custom attributes and the GDML 'name' attribute.
      G4String stepName;
      G4String geant4Name;
      G4String gdmlName;
      const xercesc::DOMNamedNodeMap* attrs = child->getAttributes();
      for (XMLSize_t i = 0; i < attrs->getLength(); ++i) {
        const auto* attr = dynamic_cast<const xercesc::DOMAttr*>(attrs->item(i));
        if (!attr) {
          continue;
        }
        const G4String aName  = Transcode(attr->getName());
        const G4String aValue = Transcode(attr->getValue());
        if (aName == "stepName") {
          stepName = aValue;
        } else if (aName == "geant4Name") {
          geant4Name = aValue;
        } else if (aName == "name") {
          gdmlName = aValue;
        }
      }

      if (stepName.empty()) {
        G4Exception("G4OCCTMaterialMapReader::ReadFile", "G4OCCT_MatReader006", FatalException,
                    ("<material> element in '" + path +
                     "' is missing the required 'stepName' "
                     "attribute.")
                        .c_str());
        continue;
      }

      G4Material* mat = nullptr;

      if (!geant4Name.empty()) {
        // ── Type 1: NIST alias ────────────────────────────────────────────
        mat = G4NistManager::Instance()->FindOrBuildMaterial(geant4Name);
        if (!mat) {
          G4Exception("G4OCCTMaterialMapReader::ReadFile", "G4OCCT_MatReader007", FatalException,
                      ("NIST material '" + geant4Name + "' not found (stepName='" + stepName +
                       "').  "
                       "Check the spelling against the Geant4 NIST material database.")
                          .c_str());
          continue;
        }
      } else {
        // ── Type 2: Inline GDML material definition ───────────────────────
        if (gdmlName.empty()) {
          G4Exception("G4OCCTMaterialMapReader::ReadFile", "G4OCCT_MatReader008", FatalException,
                      ("Inline <material stepName=\"" + stepName + "\"> in '" + path +
                       "' requires a 'name' attribute (used as the GDML material registry key).")
                          .c_str());
          continue;
        }
        // Reuse the material if it was already registered in the global
        // G4 material table (e.g. by a preceding G4GDMLParser::Read call
        // on the reference geometry).  Creating a duplicate G4Material
        // with the same name is a fatal error in Geant4.
        //
        // G4GDMLParser::Read() appends "0x<ptr>" suffixes (GenerateName)
        // and then strips them back to plain names via StripNames().  Check
        // both the generated name (pre-strip) and the plain name (post-strip)
        // so that inline materials already registered under either convention
        // are reused without re-creation.
        mat = G4Material::GetMaterial(GenerateName(gdmlName), /*warning=*/false);
        if (mat == nullptr) {
          mat = G4Material::GetMaterial(gdmlName, /*warning=*/false);
        }
        if (mat == nullptr) {
          // Material not yet registered — delegate full GDML parsing to
          // the inherited method, which creates the G4Material.
          MaterialRead(child);
          mat = GetMaterial(GenerateName(gdmlName));
          if (mat == nullptr) {
            mat = G4Material::GetMaterial(gdmlName, /*warning=*/false);
          }
        }
        if (!mat) {
          G4Exception("G4OCCTMaterialMapReader::ReadFile", "G4OCCT_MatReader009", FatalException,
                      ("Failed to create inline material '" + gdmlName + "' (stepName='" +
                       stepName + "') in '" + path +
                       "'.  Check that density and composition are valid.")
                          .c_str());
          continue;
        }
      }

      result.Add(stepName, mat);
    }
  }(); // XercesDOMParser destroyed here

  xercesc::XMLPlatformUtils::Terminate();

  if (!preFatalCode.empty()) {
    G4Exception("G4OCCTMaterialMapReader::ReadFile", preFatalCode.c_str(), FatalException,
                preFatalMsg.c_str());
    // Normally unreachable: FatalException aborts.  If a non-aborting handler
    // is installed (e.g. G4OCCTFatalCatcher in tests), return an empty map so
    // callers get a predictable, safe value.
    return result;
  }

  return result;
}
