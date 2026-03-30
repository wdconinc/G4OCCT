// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSensitiveDetectorMapReader.cc
/// @brief Implementation of G4OCCTSensitiveDetectorMapReader.

#include "G4OCCT/G4OCCTSensitiveDetectorMapReader.hh"

// Xerces — provided transitively by Geant4::G4gdml.
// G4GDMLRead.hh already includes the Xerces DOM headers; we repeat them here
// for clarity and to document the direct dependency.
#include <xercesc/dom/DOM.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

// Geant4
#include <G4Exception.hh>
#include <G4SDManager.hh>

namespace {
// Convert a Xerces XMLCh* string to G4String.  Releases the transcoded buffer
// immediately so callers do not need to manage it.
G4String Transcode(const XMLCh* const str) {
  char* buf = xercesc::XMLString::transcode(str);
  G4String result(buf);
  xercesc::XMLString::release(&buf);
  return result;
}
} // namespace

G4OCCTSensitiveDetectorMap G4OCCTSensitiveDetectorMapReader::ReadFile(const G4String& path) {
  // Xerces initialization is ref-counted; safe to call multiple times.
  xercesc::XMLPlatformUtils::Initialize();

  G4OCCTSensitiveDetectorMap result;

  // Pre-parse fatal errors (SDReader003–007) are saved here and reported
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
      preFatalCode = "G4OCCT_SDReader003";
      preFatalMsg  = "XML error parsing '" + path + "': " + Transcode(e.getMessage());
      return;
    } catch (const xercesc::DOMException& e) {
      preFatalCode = "G4OCCT_SDReader004";
      preFatalMsg  = "DOM error parsing '" + path + "': " + Transcode(e.getMessage());
      return;
    }

    const xercesc::DOMDocument* const doc = parser.getDocument();
    if (!doc) {
      preFatalCode = "G4OCCT_SDReader005";
      preFatalMsg  = "Cannot open document: " + path;
      return;
    }

    const xercesc::DOMElement* const root = doc->getDocumentElement();
    if (!root) {
      preFatalCode = "G4OCCT_SDReader006";
      preFatalMsg  = "Empty document: " + path;
      return;
    }

    const G4String rootTag = Transcode(root->getTagName());
    if (rootTag != "sensitive_detector_map") {
      preFatalCode = "G4OCCT_SDReader007";
      preFatalMsg  = "Root element must be <sensitive_detector_map>, got <" + rootTag + "> in '"
                    + path + "'";
      return;
    }

    for (xercesc::DOMNode* node = root->getFirstChild(); node != nullptr;
         node                   = node->getNextSibling()) {
      if (node->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) {
        continue;
      }
      const auto* child = dynamic_cast<const xercesc::DOMElement*>(node);
      if (!child) {
        continue;
      }
      if (Transcode(child->getTagName()) != "volume") {
        continue;
      }

      G4String volumeName;
      G4String sdName;
      const xercesc::DOMNamedNodeMap* attrs = child->getAttributes();
      for (XMLSize_t i = 0; i < attrs->getLength(); ++i) {
        const auto* attr = dynamic_cast<const xercesc::DOMAttr*>(attrs->item(i));
        if (!attr) {
          continue;
        }
        const G4String aName  = Transcode(attr->getName());
        const G4String aValue = Transcode(attr->getValue());
        if (aName == "name") {
          volumeName = aValue;
        } else if (aName == "sensDet") {
          sdName = aValue;
        }
      }

      if (volumeName.empty()) {
        G4Exception("G4OCCTSensitiveDetectorMapReader::ReadFile", "G4OCCT_SDReader000",
                    FatalException,
                    ("<volume> element in '" + path
                     + "' is missing the required 'name' attribute.")
                        .c_str());
        continue;
      }

      if (sdName.empty()) {
        G4Exception("G4OCCTSensitiveDetectorMapReader::ReadFile", "G4OCCT_SDReader001",
                    FatalException,
                    ("<volume name=\"" + volumeName + "\"> in '" + path
                     + "' is missing the required 'sensDet' attribute.")
                        .c_str());
        continue;
      }

      G4VSensitiveDetector* sd =
          G4SDManager::GetSDMpointer()->FindSensitiveDetector(sdName, /*warning=*/false);
      if (!sd) {
        G4Exception("G4OCCTSensitiveDetectorMapReader::ReadFile", "G4OCCT_SDReader002",
                    FatalException,
                    ("Sensitive detector '" + sdName + "' (volume pattern '" + volumeName
                     + "') not found in G4SDManager.  Ensure all SDs are registered before "
                       "calling ReadFile().")
                        .c_str());
        continue;
      }

      result.Add(volumeName, sd);
    }
  }(); // XercesDOMParser destroyed here

  xercesc::XMLPlatformUtils::Terminate();

  if (!preFatalCode.empty()) {
    G4Exception("G4OCCTSensitiveDetectorMapReader::ReadFile", preFatalCode.c_str(),
                FatalException, preFatalMsg.c_str());
    // Normally unreachable: FatalException aborts.  If a non-aborting handler
    // is installed (e.g. G4OCCTFatalCatcher in tests), return an empty map so
    // callers get a predictable, safe value.
    return result;
  }

  return result;
}
