// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/yaml_subset.hh"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace g4occt::tests::geometry {
namespace {

  struct ParsedLine {
    std::size_t number{0};
    int indent{0};
    std::string content;
  };

  bool LooksLikeKeyValue(const std::string_view text) {
    return text.find(':') != std::string_view::npos;
  }

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

  std::string Unquote(std::string value) {
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
      return value.substr(1, value.size() - 2);
    }
    return value;
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
        const bool is_continuation = !lines.empty() && indent > lines.back().indent &&
                                     trimmed.rfind("- ", 0) != 0 && !LooksLikeKeyValue(trimmed);
        if (is_continuation) {
          lines.back().content += " " + trimmed;
          continue;
        }
        lines.push_back(ParsedLine{line_number, indent, trimmed});
      }
    }
    return lines;
  }

  bool IsInlineList(const std::string_view value) {
    return value.size() >= 2 && value.front() == '[' && value.back() == ']';
  }

  std::pair<std::string, std::string> ParseKeyValue(const ParsedLine& line,
                                                    const std::filesystem::path& path) {
    const auto separator = line.content.find(':');
    if (separator == std::string::npos) {
      throw std::runtime_error("Expected key/value pair in " + path.string() + ":" +
                               std::to_string(line.number));
    }

    return {Trim(line.content.substr(0, separator)), Trim(line.content.substr(separator + 1))};
  }

  YamlNode ParseValue(const std::string& value);
  YamlNode ParseBlock(const std::vector<ParsedLine>& lines, std::size_t& index, int indent,
                      const std::filesystem::path& path);

  YamlNode ParseValue(const std::string& value) {
    if (IsInlineList(value)) {
      YamlNode::SequenceType items;
      const std::string inner = Trim(value.substr(1, value.size() - 2));
      if (!inner.empty()) {
        std::stringstream stream(inner);
        std::string token;
        while (std::getline(stream, token, ',')) {
          items.push_back(YamlNode::Scalar(Unquote(Trim(token))));
        }
      }
      return YamlNode::Sequence(std::move(items));
    }

    return YamlNode::Scalar(Unquote(value));
  }

  YamlNode ParseMapping(const std::vector<ParsedLine>& lines, std::size_t& index, const int indent,
                        const std::filesystem::path& path) {
    YamlNode::MappingType values;
    while (index < lines.size()) {
      const ParsedLine& line = lines[index];
      if (line.indent < indent) {
        break;
      }
      if (line.indent != indent || line.content.rfind("- ", 0) == 0) {
        break;
      }

      const auto [key, value] = ParseKeyValue(line, path);
      ++index;
      if (value.empty()) {
        if (index >= lines.size() || lines[index].indent <= indent) {
          throw std::runtime_error("Expected nested block for key '" + key + "' in " +
                                   path.string() + ":" + std::to_string(line.number));
        }
        values.emplace(key, ParseBlock(lines, index, indent + 2, path));
      } else {
        values.emplace(key, ParseValue(value));
      }
    }

    return YamlNode::Mapping(std::move(values));
  }

  YamlNode ParseSequence(const std::vector<ParsedLine>& lines, std::size_t& index, const int indent,
                         const std::filesystem::path& path) {
    YamlNode::SequenceType values;
    while (index < lines.size()) {
      const ParsedLine& line = lines[index];
      if (line.indent < indent) {
        break;
      }
      if (line.indent != indent || line.content.rfind("- ", 0) != 0) {
        break;
      }

      const std::string rest = Trim(line.content.substr(2));
      if (rest.empty()) {
        ++index;
        if (index >= lines.size() || lines[index].indent <= indent) {
          throw std::runtime_error("Expected nested list entry in " + path.string() + ":" +
                                   std::to_string(line.number));
        }
        values.push_back(ParseBlock(lines, index, indent + 2, path));
        continue;
      }

      if (!IsInlineList(rest) && LooksLikeKeyValue(rest)) {
        YamlNode::MappingType item;
        ParsedLine inline_line{line.number, indent + 2, rest};
        const auto [key, value] = ParseKeyValue(inline_line, path);
        ++index;
        if (value.empty()) {
          if (index >= lines.size() || lines[index].indent <= indent + 2) {
            throw std::runtime_error("Expected nested mapping value for key '" + key + "' in " +
                                     path.string() + ":" + std::to_string(line.number));
          }
          item.emplace(key, ParseBlock(lines, index, indent + 4, path));
        } else {
          item.emplace(key, ParseValue(value));
        }

        while (index < lines.size()) {
          const ParsedLine& nested = lines[index];
          if (nested.indent <= indent) {
            break;
          }
          if (nested.indent != indent + 2 || nested.content.rfind("- ", 0) == 0) {
            break;
          }
          const auto [nested_key, nested_value] = ParseKeyValue(nested, path);
          ++index;
          if (nested_value.empty()) {
            if (index >= lines.size() || lines[index].indent <= indent + 2) {
              throw std::runtime_error("Expected nested block for key '" + nested_key + "' in " +
                                       path.string() + ":" + std::to_string(nested.number));
            }
            item.emplace(nested_key, ParseBlock(lines, index, indent + 4, path));
          } else {
            item.emplace(nested_key, ParseValue(nested_value));
          }
        }

        values.push_back(YamlNode::Mapping(std::move(item)));
        continue;
      }

      values.push_back(ParseValue(rest));
      ++index;
    }

    return YamlNode::Sequence(std::move(values));
  }

  YamlNode ParseBlock(const std::vector<ParsedLine>& lines, std::size_t& index, const int indent,
                      const std::filesystem::path& path) {
    if (index >= lines.size()) {
      throw std::runtime_error("Unexpected end of YAML while parsing " + path.string());
    }

    if (lines[index].content.rfind("- ", 0) == 0) {
      return ParseSequence(lines, index, indent, path);
    }
    return ParseMapping(lines, index, indent, path);
  }

} // namespace

YamlNode::YamlNode() : type_(Type::kScalar) {}

YamlNode YamlNode::Scalar(std::string value) {
  YamlNode node;
  node.type_   = Type::kScalar;
  node.scalar_ = std::move(value);
  node.sequence_.clear();
  node.mapping_.clear();
  return node;
}

YamlNode YamlNode::Sequence(SequenceType values) {
  YamlNode node;
  node.type_     = Type::kSequence;
  node.sequence_ = std::move(values);
  node.scalar_.clear();
  node.mapping_.clear();
  return node;
}

YamlNode YamlNode::Mapping(MappingType values) {
  YamlNode node;
  node.type_    = Type::kMapping;
  node.mapping_ = std::move(values);
  node.scalar_.clear();
  node.sequence_.clear();
  return node;
}

YamlNode::Type YamlNode::GetType() const { return type_; }

bool YamlNode::IsScalar() const { return type_ == Type::kScalar; }

bool YamlNode::IsSequence() const { return type_ == Type::kSequence; }

bool YamlNode::IsMapping() const { return type_ == Type::kMapping; }

const std::string& YamlNode::AsScalar() const {
  if (!IsScalar()) {
    throw std::runtime_error("Requested scalar value from non-scalar YAML node");
  }
  return scalar_;
}

const YamlNode::SequenceType& YamlNode::AsSequence() const {
  if (!IsSequence()) {
    throw std::runtime_error("Requested sequence value from non-sequence YAML node");
  }
  return sequence_;
}

const YamlNode::MappingType& YamlNode::AsMapping() const {
  if (!IsMapping()) {
    throw std::runtime_error("Requested mapping value from non-mapping YAML node");
  }
  return mapping_;
}

YamlNode ParseYamlSubsetFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open YAML file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::vector<ParsedLine> lines = ParseLines(buffer.str());
  if (lines.empty()) {
    return YamlNode::Mapping({});
  }

  std::size_t index = 0;
  YamlNode document = ParseBlock(lines, index, lines.front().indent, path);
  if (index != lines.size()) {
    throw std::runtime_error("Did not consume full YAML document: " + path.string());
  }
  return document;
}

} // namespace g4occt::tests::geometry
