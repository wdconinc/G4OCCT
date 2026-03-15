// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_YAML_SUBSET_HH
#define G4OCCT_TESTS_GEOMETRY_YAML_SUBSET_HH

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace g4occt::tests::geometry {

/** Minimal YAML node used by the fixture provenance parser. */
class YamlNode {
public:
  enum class Type {
    kScalar,
    kSequence,
    kMapping,
  };

  using SequenceType = std::vector<YamlNode>;
  using MappingType  = std::map<std::string, YamlNode>;

  YamlNode();

  static YamlNode Scalar(std::string value);
  static YamlNode Sequence(SequenceType values);
  static YamlNode Mapping(MappingType values);

  Type GetType() const;
  bool IsScalar() const;
  bool IsSequence() const;
  bool IsMapping() const;

  const std::string& AsScalar() const;
  const SequenceType& AsSequence() const;
  const MappingType& AsMapping() const;

private:
  Type type_;
  std::string scalar_;
  SequenceType sequence_;
  MappingType mapping_;
};

/** Parse the repository's narrow YAML subset into a typed node tree. */
YamlNode ParseYamlSubsetFile(const std::filesystem::path& path);

} // namespace g4occt::tests::geometry

#endif
