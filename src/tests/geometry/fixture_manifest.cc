// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_manifest.hh"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace g4occt::tests::geometry {
namespace {

  struct ParsedLine {
    std::size_t number{0};
    int indent{0};
    std::string content;
  };

  std::string Trim(const std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
      ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
      --end;
    }

    return std::string(text.substr(begin, end - begin));
  }

  std::string StripComment(const std::string_view text) {
    const auto comment_pos = text.find('#');
    if (comment_pos == std::string_view::npos) {
      return std::string(text);
    }
    return std::string(text.substr(0, comment_pos));
  }

  std::vector<ParsedLine> ParseLines(const std::string& text) {
    std::vector<ParsedLine> lines;
    std::istringstream input(text);
    std::string raw_line;
    std::size_t line_number = 0;
    while (std::getline(input, raw_line)) {
      ++line_number;
      if (!raw_line.empty() && raw_line.back() == '\r') {
        raw_line.pop_back();
      }

      int indent = 0;
      while (indent < static_cast<int>(raw_line.size()) &&
             raw_line[static_cast<std::size_t>(indent)] == ' ') {
        ++indent;
      }

      const std::string without_comments = StripComment(raw_line);
      const std::string trimmed          = Trim(without_comments);
      if (!trimmed.empty()) {
        lines.push_back(ParsedLine{line_number, indent, trimmed});
      }
    }
    return lines;
  }

  std::pair<std::string, std::string> ParseKeyValue(const ParsedLine& line,
                                                    const std::filesystem::path& manifest_path) {
    const auto separator = line.content.find(':');
    if (separator == std::string::npos) {
      throw std::runtime_error("Expected key/value pair in " + manifest_path.string() + ":" +
                               std::to_string(line.number));
    }

    const std::string key = Trim(line.content.substr(0, separator));
    std::string value     = Trim(line.content.substr(separator + 1));
    if (!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                           (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }
    return {key, value};
  }

  bool IsInlineList(const std::string_view value) {
    return value.size() >= 2 && value.front() == '[' && value.back() == ']';
  }

  std::vector<std::string> ParseInlineList(const std::string_view value) {
    std::vector<std::string> items;
    if (!IsInlineList(value)) {
      return items;
    }

    const std::string inner = Trim(value.substr(1, value.size() - 2));
    if (inner.empty()) {
      return items;
    }

    std::stringstream stream(inner);
    std::string token;
    while (std::getline(stream, token, ',')) {
      std::string item = Trim(token);
      if (!item.empty() && ((item.front() == '"' && item.back() == '"') ||
                            (item.front() == '\'' && item.back() == '\''))) {
        item = item.substr(1, item.size() - 2);
      }
      if (!item.empty()) {
        items.push_back(std::move(item));
      }
    }

    return items;
  }

  std::vector<std::string> ParseScalarList(const std::vector<ParsedLine>& lines, std::size_t& index,
                                           const int parent_indent,
                                           const std::filesystem::path& manifest_path) {
    std::vector<std::string> items;
    while (index < lines.size()) {
      const ParsedLine& line = lines[index];
      if (line.indent <= parent_indent) {
        break;
      }
      if (line.indent != parent_indent + 2 || line.content.rfind("- ", 0) != 0) {
        throw std::runtime_error("Expected list item in " + manifest_path.string() + ":" +
                                 std::to_string(line.number));
      }
      items.push_back(Trim(line.content.substr(2)));
      ++index;
    }
    return items;
  }

  double ParseDouble(const std::string& text, const ParsedLine& line,
                     const std::filesystem::path& path) {
    try {
      return std::stod(text);
    } catch (const std::exception&) {
      throw std::runtime_error("Expected numeric value in " + path.string() + ":" +
                               std::to_string(line.number));
    }
  }

  void ApplyFixtureField(FixtureReference& fixture, const std::string& key,
                         const std::string& value, const ParsedLine& line,
                         const std::filesystem::path& manifest_path) {
    if (key == "id") {
      fixture.id = value;
    } else if (key == "geant4_class") {
      fixture.geant4_class = value;
    } else if (key == "family") {
      fixture.family = value;
    } else if (key == "fixture_slug") {
      fixture.fixture_slug = value;
    } else if (key == "relative_directory") {
      fixture.relative_directory = value;
    } else if (key == "step_file") {
      fixture.step_file = value;
    } else if (key == "provenance_file") {
      fixture.provenance_file = value;
    } else if (key == "validation_state") {
      fixture.validation_state = ParseFixtureValidationState(value);
    } else {
      throw std::runtime_error("Unsupported fixture field '" + key + "' in " +
                               manifest_path.string() + ":" + std::to_string(line.number));
    }
  }

  void ApplyExpectationField(FixtureExpectation& expectation, const std::string& key,
                             const std::string& value, const ParsedLine& line,
                             const std::filesystem::path& manifest_path) {
    if (key == "quantity") {
      expectation.quantity = value;
    } else if (key == "value") {
      expectation.value = ParseDouble(value, line, manifest_path);
    } else if (key == "absolute_tolerance") {
      expectation.absolute_tolerance = ParseDouble(value, line, manifest_path);
    } else if (key == "unit") {
      expectation.unit = value;
    } else {
      throw std::runtime_error("Unsupported expectation field '" + key + "' in " +
                               manifest_path.string() + ":" + std::to_string(line.number));
    }
  }

  std::vector<FixtureExpectation> ParseExpectations(const std::vector<ParsedLine>& lines,
                                                    std::size_t& index, const int parent_indent,
                                                    const std::filesystem::path& manifest_path) {
    std::vector<FixtureExpectation> expectations;
    while (index < lines.size()) {
      const ParsedLine& line = lines[index];
      if (line.indent <= parent_indent) {
        break;
      }
      if (line.indent != parent_indent + 2 || line.content.rfind("-", 0) != 0) {
        throw std::runtime_error("Expected expectation list item in " + manifest_path.string() +
                                 ":" + std::to_string(line.number));
      }

      FixtureExpectation expectation;
      std::string rest = Trim(line.content.substr(1));
      if (!rest.empty()) {
        const auto [key, value] =
            ParseKeyValue(ParsedLine{line.number, line.indent, rest}, manifest_path);
        ApplyExpectationField(expectation, key, value, line, manifest_path);
      }
      ++index;

      while (index < lines.size()) {
        const ParsedLine& nested = lines[index];
        if (nested.indent <= line.indent) {
          break;
        }
        if (nested.indent != parent_indent + 4) {
          throw std::runtime_error("Unexpected indentation for expectation field in " +
                                   manifest_path.string() + ":" + std::to_string(nested.number));
        }
        const auto [key, value] = ParseKeyValue(nested, manifest_path);
        ApplyExpectationField(expectation, key, value, nested, manifest_path);
        ++index;
      }

      expectations.push_back(std::move(expectation));
    }
    return expectations;
  }

  std::vector<FixtureReference> ParseFixtures(const std::vector<ParsedLine>& lines,
                                              std::size_t& index, const int parent_indent,
                                              const std::filesystem::path& manifest_path) {
    std::vector<FixtureReference> fixtures;
    while (index < lines.size()) {
      const ParsedLine& line = lines[index];
      if (line.indent <= parent_indent) {
        break;
      }
      if (line.indent != parent_indent + 2 || line.content.rfind("-", 0) != 0) {
        throw std::runtime_error("Expected fixture list item in " + manifest_path.string() + ":" +
                                 std::to_string(line.number));
      }

      FixtureReference fixture;
      std::string rest = Trim(line.content.substr(1));
      if (!rest.empty()) {
        const auto [key, value] =
            ParseKeyValue(ParsedLine{line.number, line.indent, rest}, manifest_path);
        ApplyFixtureField(fixture, key, value, line, manifest_path);
      }
      ++index;

      while (index < lines.size()) {
        const ParsedLine& nested = lines[index];
        if (nested.indent <= line.indent) {
          break;
        }
        if (nested.indent != parent_indent + 4) {
          throw std::runtime_error("Unexpected indentation for fixture field in " +
                                   manifest_path.string() + ":" + std::to_string(nested.number));
        }

        const auto [key, value] = ParseKeyValue(nested, manifest_path);
        ++index;
        if (key == "reused_by") {
          if (value.empty()) {
            fixture.reused_by = ParseScalarList(lines, index, nested.indent, manifest_path);
          } else {
            fixture.reused_by = ParseInlineList(value);
          }
          continue;
        }
        if (key == "expectations") {
          if (value.empty()) {
            fixture.expectations = ParseExpectations(lines, index, nested.indent, manifest_path);
          } else if (value == "[]") {
            fixture.expectations.clear();
          } else {
            throw std::runtime_error("Expectations must be a block list in " +
                                     manifest_path.string() + ":" + std::to_string(nested.number));
          }
          continue;
        }

        ApplyFixtureField(fixture, key, value, nested, manifest_path);
      }

      fixtures.push_back(std::move(fixture));
    }
    return fixtures;
  }

} // namespace

std::string ToString(const FixtureValidationState state) {
  switch (state) {
  case FixtureValidationState::kPlanned:
    return "planned";
  case FixtureValidationState::kValidated:
    return "validated";
  case FixtureValidationState::kRejected:
    return "rejected";
  }

  throw std::invalid_argument("Unsupported fixture validation state");
}

FixtureValidationState ParseFixtureValidationState(const std::string_view token) {
  if (token == "planned") {
    return FixtureValidationState::kPlanned;
  }
  if (token == "validated") {
    return FixtureValidationState::kValidated;
  }
  if (token == "rejected") {
    return FixtureValidationState::kRejected;
  }

  throw std::invalid_argument("Unknown fixture validation state token: " + std::string(token));
}

FixtureManifest MakeFixtureManifest(const std::filesystem::path& source_path, std::string family,
                                    std::string description) {
  FixtureManifest manifest;
  manifest.source_path = source_path;
  manifest.family      = std::move(family);
  manifest.description = std::move(description);
  return manifest;
}

void AddCoverageClass(FixtureManifest& manifest, std::string class_name) {
  const auto existing =
      std::find(manifest.coverage_classes.begin(), manifest.coverage_classes.end(), class_name);
  if (existing == manifest.coverage_classes.end()) {
    manifest.coverage_classes.push_back(std::move(class_name));
  }
}

void AddFixture(FixtureManifest& manifest, FixtureReference fixture) {
  manifest.fixtures.push_back(std::move(fixture));
}

std::filesystem::path ResolveRepositoryFixtureRoot(const FixtureRepositoryManifest& manifest) {
  return manifest.source_path.parent_path();
}

std::filesystem::path ResolveFamilyManifestPath(const FixtureRepositoryManifest& manifest,
                                                const std::string_view family) {
  return ResolveRepositoryFixtureRoot(manifest) / std::string(family) / "manifest.yaml";
}

std::filesystem::path ResolveFixtureDirectory(const FixtureManifest& manifest,
                                              const FixtureReference& fixture) {
  return manifest.source_path.parent_path() / fixture.relative_directory;
}

std::filesystem::path ResolveFixtureStepPath(const FixtureManifest& manifest,
                                             const FixtureReference& fixture) {
  return ResolveFixtureDirectory(manifest, fixture) / fixture.step_file;
}

std::filesystem::path ResolveFixtureProvenancePath(const FixtureManifest& manifest,
                                                   const FixtureReference& fixture) {
  return ResolveFixtureDirectory(manifest, fixture) / fixture.provenance_file;
}

std::string ReadManifestText(const std::filesystem::path& manifest_path) {
  std::ifstream input(manifest_path);
  if (!input) {
    throw std::runtime_error("Unable to open manifest: " + manifest_path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

FixtureRepositoryManifest
ParseFixtureRepositoryManifest(const std::filesystem::path& manifest_path) {
  FixtureRepositoryManifest manifest;
  manifest.source_path = manifest_path;

  const auto lines = ParseLines(ReadManifestText(manifest_path));
  for (std::size_t index = 0; index < lines.size();) {
    const ParsedLine& line = lines[index];
    if (line.indent != 0) {
      throw std::runtime_error("Unexpected indentation in " + manifest_path.string() + ":" +
                               std::to_string(line.number));
    }

    const auto [key, value] = ParseKeyValue(line, manifest_path);
    ++index;
    if (key == "schema_version") {
      manifest.schema_version = value;
    } else if (key == "fixture_root") {
      manifest.fixture_root = value;
    } else if (key == "owner") {
      manifest.owner = value;
    } else if (key == "policy") {
      while (index < lines.size() && lines[index].indent > line.indent) {
        const ParsedLine& nested = lines[index];
        if (nested.indent != line.indent + 2) {
          throw std::runtime_error("Unexpected indentation in policy block at " +
                                   manifest_path.string() + ":" + std::to_string(nested.number));
        }
        const auto [policy_key, policy_value] = ParseKeyValue(nested, manifest_path);
        if (policy_key == "step_file_name") {
          manifest.policy.step_file_name = policy_value;
        } else if (policy_key == "provenance_file_name") {
          manifest.policy.provenance_file_name = policy_value;
        } else if (policy_key == "volume_unit") {
          manifest.policy.volume_unit = policy_value;
        } else if (policy_key == "validation_status_values") {
          manifest.policy.validation_status_values = ParseInlineList(policy_value);
        } else {
          throw std::runtime_error("Unsupported policy field '" + policy_key + "' in " +
                                   manifest_path.string() + ":" + std::to_string(nested.number));
        }
        ++index;
      }
    } else if (key == "families") {
      if (!value.empty() && value != "[]") {
        throw std::runtime_error("Families must be encoded as a block list in " +
                                 manifest_path.string() + ":" + std::to_string(line.number));
      }
      manifest.families = ParseScalarList(lines, index, line.indent, manifest_path);
    } else {
      throw std::runtime_error("Unsupported repository manifest field '" + key + "' in " +
                               manifest_path.string() + ":" + std::to_string(line.number));
    }
  }

  return manifest;
}

FixtureManifest ParseFixtureManifestFile(const std::filesystem::path& manifest_path) {
  FixtureManifest manifest;
  manifest.source_path = manifest_path;

  const auto lines = ParseLines(ReadManifestText(manifest_path));
  for (std::size_t index = 0; index < lines.size();) {
    const ParsedLine& line = lines[index];
    if (line.indent != 0) {
      throw std::runtime_error("Unexpected indentation in " + manifest_path.string() + ":" +
                               std::to_string(line.number));
    }

    const auto [key, value] = ParseKeyValue(line, manifest_path);
    ++index;
    if (key == "schema_version") {
      manifest.schema_version = value;
    } else if (key == "family") {
      manifest.family = value;
    } else if (key == "summary") {
      manifest.description = value;
    } else if (key == "coverage_classes") {
      if (value.empty()) {
        manifest.coverage_classes = ParseScalarList(lines, index, line.indent, manifest_path);
      } else {
        manifest.coverage_classes = ParseInlineList(value);
      }
    } else if (key == "fixtures") {
      if (value.empty()) {
        manifest.fixtures = ParseFixtures(lines, index, line.indent, manifest_path);
      } else if (value == "[]") {
        manifest.fixtures.clear();
      } else {
        throw std::runtime_error("Fixtures must be encoded as a block list in " +
                                 manifest_path.string() + ":" + std::to_string(line.number));
      }
    } else {
      throw std::runtime_error("Unsupported family manifest field '" + key + "' in " +
                               manifest_path.string() + ":" + std::to_string(line.number));
    }
  }

  for (auto& fixture : manifest.fixtures) {
    if (fixture.family.empty()) {
      fixture.family = manifest.family;
    }
  }

  return manifest;
}

} // namespace g4occt::tests::geometry
