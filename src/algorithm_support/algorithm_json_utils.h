#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "cJSON.h"

namespace algorithm_support {
namespace json_utils {

inline std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

inline std::string TrimText(std::string value) {
  const auto is_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };
  const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  return begin >= end ? std::string{} : std::string(begin, end);
}

inline bool TryParsePositiveUint(std::string_view text, uint32_t* out_value) {
  if (!out_value || text.empty()) {
    return false;
  }
  uint64_t value = 0u;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10u + static_cast<uint64_t>(ch - '0');
    if (value > UINT32_MAX) {
      return false;
    }
  }
  if (value == 0u) {
    return false;
  }
  *out_value = static_cast<uint32_t>(value);
  return true;
}

inline uint32_t ExtractPackagePrecisionBitsFromText(
  const std::string& text,
  const std::string& key,
  uint32_t fallback = 32u) {
  const std::string needle = "\"" + key + "\"";
  const std::string::size_type key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    return fallback;
  }
  const std::string::size_type colon_pos = text.find(':', key_pos + needle.size());
  if (colon_pos == std::string::npos) {
    return fallback;
  }
  const std::string::size_type first_quote = text.find('"', colon_pos + 1u);
  if (first_quote == std::string::npos) {
    return fallback;
  }
  const std::string::size_type second_quote = text.find('"', first_quote + 1u);
  if (second_quote == std::string::npos || second_quote <= first_quote + 1u) {
    return fallback;
  }
  const std::string token = text.substr(first_quote + 1u, second_quote - first_quote - 1u);
  uint32_t parsed = 0u;
  if (!TryParsePositiveUint(token, &parsed)) {
    return fallback;
  }
  return parsed;
}

inline bool TryEvaluatePriciseArrayToken(
  const std::string& token,
  uint32_t default_precision_bits,
  std::string* out_replacement) {
  if (!out_replacement) {
    return false;
  }
  out_replacement->clear();
  const std::string trimmed = TrimText(token);
  if (trimmed.empty()) {
    return false;
  }

  const std::string::size_type slash = trimmed.find('/');
  if (slash == std::string::npos) {
    return false;
  }

  std::string left = TrimText(trimmed.substr(0, slash));
  const std::string right = TrimText(trimmed.substr(slash + 1u));
  std::string normalized_left{};
  normalized_left.reserve(left.size());
  for (char ch : left) {
    normalized_left.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  if (normalized_left != "defaultprecision") {
    return false;
  }

  uint32_t divisor = 0u;
  if (!TryParsePositiveUint(right, &divisor) || divisor == 0u) {
    return false;
  }
  if (default_precision_bits == 0u || default_precision_bits % divisor != 0u) {
    return false;
  }

  *out_replacement = std::to_string(default_precision_bits / divisor);
  return true;
}

inline std::string NormalizeAlgorithmPackageJsonText(std::string text) {
  if (text.empty()) {
    return text;
  }

  const uint32_t default_precision_bits = ExtractPackagePrecisionBitsFromText(text, "defaultPrecision", 32u);
  std::string output{};
  output.reserve(text.size());

  bool in_string = false;
  bool escape = false;
  for (size_t i = 0u; i < text.size(); ++i) {
    const char ch = text[i];
    output.push_back(ch);
    if (escape) {
      escape = false;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string || ch != ':') {
      continue;
    }

    size_t back = output.size();
    while (back > 0u && std::isspace(static_cast<unsigned char>(output[back - 1u])) != 0) {
      --back;
    }
    if (back < 9u || output.substr(back - 9u, 9u) != "\"pricise\"") {
      continue;
    }

    size_t cursor = i + 1u;
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      output.push_back(text[cursor]);
      ++cursor;
    }
    if (cursor >= text.size() || text[cursor] != '[') {
      i = cursor > 0u ? cursor - 1u : cursor;
      continue;
    }

    output.push_back('[');
    ++cursor;
    std::string token{};
    bool array_in_string = false;
    bool array_escape = false;
    while (cursor < text.size()) {
      const char array_ch = text[cursor];
      if (array_escape) {
        token.push_back(array_ch);
        array_escape = false;
        ++cursor;
        continue;
      }
      if (array_ch == '\\') {
        token.push_back(array_ch);
        array_escape = true;
        ++cursor;
        continue;
      }
      if (array_ch == '"') {
        token.push_back(array_ch);
        array_in_string = !array_in_string;
        ++cursor;
        continue;
      }
      if (!array_in_string && (array_ch == ',' || array_ch == ']')) {
        std::string replacement{};
        const std::string trimmed_token = TrimText(token);
        if (!trimmed_token.empty() &&
            TryEvaluatePriciseArrayToken(trimmed_token, default_precision_bits, &replacement)) {
          output.append(replacement);
        } else {
          output.append(token);
        }
        token.clear();
        output.push_back(array_ch);
        ++cursor;
        if (array_ch == ']') {
          break;
        }
        continue;
      }

      token.push_back(array_ch);
      ++cursor;
    }

    i = cursor > 0u ? cursor - 1u : cursor;
  }

  return output;
}

inline std::string ReadAlgorithmPackageJsonFile(const std::filesystem::path& path) {
  struct CachedJsonEntry {
    uintmax_t file_size{0u};
    std::filesystem::file_time_type write_time{};
    std::string normalized_text{};
    bool valid{false};
  };

  static std::mutex cache_mutex;
  static std::unordered_map<std::string, CachedJsonEntry> cache;

  std::error_code ec;
  const uintmax_t file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return {};
  }
  const std::filesystem::file_time_type write_time = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return {};
  }

  const std::string cache_key = path.generic_string();
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto found = cache.find(cache_key);
    if (found != cache.end() &&
        found->second.valid &&
        found->second.file_size == file_size &&
        found->second.write_time == write_time) {
      return found->second.normalized_text;
    }
  }

  const std::string raw_text = ReadTextFile(path);
  if (raw_text.empty()) {
    return {};
  }
  const std::string normalized_text = NormalizeAlgorithmPackageJsonText(raw_text);
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache[cache_key] = CachedJsonEntry{
      .file_size = file_size,
      .write_time = write_time,
      .normalized_text = normalized_text,
      .valid = true,
    };
  }
  return normalized_text;
}

inline std::string GetStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

inline uint32_t GetUintField(const cJSON* object, const char* key, uint32_t fallback = 0u) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item) || item->valuedouble < 0.0) {
    return fallback;
  }
  return static_cast<uint32_t>(item->valuedouble);
}

inline bool GetBoolField(const cJSON* object, const char* key, bool fallback = false) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item) {
    return fallback;
  }
  if (item->type & (cJSON_True | cJSON_False)) {
    return (item->type & cJSON_True) != 0;
  }
  return fallback;
}

inline std::vector<std::string> GetStringList(const cJSON* item) {
  std::vector<std::string> values{};
  if (!item) {
    return values;
  }
  if (cJSON_IsString(item) && item->valuestring) {
    values.push_back(item->valuestring);
    return values;
  }
  if (!cJSON_IsArray(item)) {
    return values;
  }
  const int count = cJSON_GetArraySize(item);
  values.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* entry = cJSON_GetArrayItem(item, i);
    if (!entry || !cJSON_IsString(entry) || !entry->valuestring) {
      continue;
    }
    values.emplace_back(entry->valuestring);
  }
  return values;
}

inline std::vector<uint32_t> GetUintList(const cJSON* item) {
  std::vector<uint32_t> values{};
  if (!item || !cJSON_IsArray(item)) {
    return values;
  }
  const int count = cJSON_GetArraySize(item);
  values.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* entry = cJSON_GetArrayItem(item, i);
    if (!entry || !cJSON_IsNumber(entry) || entry->valuedouble < 0.0) {
      continue;
    }
    values.push_back(static_cast<uint32_t>(entry->valuedouble));
  }
  return values;
}

inline std::vector<uint32_t> GetShapeField(const cJSON* object) {
  std::vector<uint32_t> shape{};
  if (!object || !cJSON_IsObject(object)) {
    return shape;
  }

  const cJSON* shape_item = cJSON_GetObjectItemCaseSensitive(object, "shape");
  if (!shape_item || !cJSON_IsArray(shape_item)) {
    return shape;
  }

  const int count = cJSON_GetArraySize(shape_item);
  shape.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* dim = cJSON_GetArrayItem(shape_item, i);
    if (!dim || !cJSON_IsNumber(dim) || dim->valuedouble < 0.0) {
      continue;
    }
    shape.push_back(static_cast<uint32_t>(dim->valuedouble));
  }
  return shape;
}

}  // namespace json_utils
}  // namespace algorithm_support
