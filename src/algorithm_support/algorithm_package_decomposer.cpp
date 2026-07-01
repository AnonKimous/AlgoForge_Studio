#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_abi.h"

#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_json_utils.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"

#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <utility>

namespace algorithm_support {

namespace {

namespace fs = std::filesystem;

void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

bool _TryParseScalarPrecisionBits(std::string_view text, uint32_t* out_bits) {
  if (!out_bits) {
    return false;
  }
  if (text == "8" || text == "i8" || text == "u8") {
    *out_bits = 8u;
    return true;
  }
  if (text == "16" || text == "i16" || text == "u16" || text == "fp16" || text == "float16") {
    *out_bits = 16u;
    return true;
  }
  if (text == "32" || text == "i32" || text == "u32" || text == "fp32" || text == "float32" || text == "float") {
    *out_bits = 32u;
    return true;
  }
  if (text == "64" || text == "i64" || text == "u64" || text == "fp64" || text == "float64" || text == "double") {
    *out_bits = 64u;
    return true;
  }
  return false;
}

bool _ParseScalarPrecisionBits(
  std::string_view text,
  const std::string& package_path,
  uint32_t* out_bits,
  std::string* out_error_message) {
  if (_TryParseScalarPrecisionBits(text, out_bits)) {
    return true;
  }
  _SetErrorMessage(
    out_error_message,
    "Unsupported precision '" + std::string(text) + "' in package JSON: " + package_path);
  return false;
}

bool _NormalizeDescriptorValueCodec(
  std::string_view text,
  std::string* out_codec) {
  if (!out_codec) {
    return false;
  }
  if (text.empty() || text == "float" || text == "fp" || text == "fp32" || text == "fp64") {
    *out_codec = "float";
    return true;
  }
  if (text == "ieee754" || text == "ieee754_float" || text == "ieee_float") {
    *out_codec = "ieee754";
    return true;
  }
  if (text == "int" || text == "signed_int" || text == "sint") {
    *out_codec = "int";
    return true;
  }
  if (text == "uint" || text == "unsigned_int") {
    *out_codec = "uint";
    return true;
  }
  return false;
}

bool _WriteDescriptorScalarValue(
  algorithm::AlgorithmContainer* container,
  size_t byte_offset,
  uint32_t scalar_bits,
  const std::string& value_codec,
  double value,
  std::string* out_error_message);

struct PackageDecomposerMeshBinding {
  std::string resource_kind;
  std::string resource_name;
  std::string container_name;
};

struct PackageDecomposerContainerInfo {
  uint32_t scalar_bits{32u};
  algorithm::AlgorithmContainerStorageKind storage_kind{algorithm::AlgorithmContainerStorageKind::TemporaryRegister};
};

struct PackageDecomposerDescriptionBinding {
  std::vector<std::string> from_names;
  std::string target_container_name;
  std::vector<uint32_t> target_indices;
  std::vector<std::string> target_container_names;
  std::string value_codec{"float"};
  uint32_t source_scalar_bits{0u};
  std::vector<uint32_t> packed_segment_bits;
};

struct PackageDecomposerDescriptionEntry {
  std::string name;
  std::vector<PackageDecomposerDescriptionBinding> bindings;
};

struct PackageDecomposerSchema {
  uint32_t default_scalar_bits{32u};
  std::unordered_map<std::string, PackageDecomposerContainerInfo> container_infos;
  std::unordered_map<std::string, std::vector<std::string>> container_aliases_by_name;
  std::vector<PackageDecomposerMeshBinding> mesh_bindings;
  std::vector<PackageDecomposerDescriptionEntry> description_entries;
  bool valid{false};
  std::string error_message;
};

bool _RegisterContainerInfo(
  const std::string& container_name,
  uint32_t scalar_bits,
  algorithm::AlgorithmContainerStorageKind storage_kind,
  const std::string& package_path,
  PackageDecomposerSchema* out_schema,
  std::string* out_error_message) {
  if (!out_schema) {
    _SetErrorMessage(out_error_message, "Package decomposer schema output pointer is null.");
    return false;
  }
  if (container_name.empty()) {
    _SetErrorMessage(out_error_message, "Package container name is empty: " + package_path);
    return false;
  }
  auto [_, inserted] = out_schema->container_infos.emplace(
    container_name,
    PackageDecomposerContainerInfo{
      .scalar_bits = scalar_bits,
      .storage_kind = storage_kind,
    });
  if (!inserted) {
    _SetErrorMessage(
      out_error_message,
      "Package container name is duplicated in schema: " + container_name + " (" + package_path + ")");
    return false;
  }
  return true;
}

bool _LoadContainerInfos(
  const cJSON* root,
  const std::string& package_path,
  PackageDecomposerSchema* out_schema,
  std::string* out_error_message) {
  if (!root || !cJSON_IsObject(root) || !out_schema) {
    _SetErrorMessage(out_error_message, "Invalid package JSON root while loading container schema.");
    return false;
  }

  uint32_t default_bits = 32u;
  const cJSON* global_cfg = cJSON_GetObjectItemCaseSensitive(root, "globalCfg");
  if (global_cfg) {
    if (!cJSON_IsObject(global_cfg)) {
      _SetErrorMessage(out_error_message, "Package globalCfg must be an object: " + package_path);
      return false;
    }
    const cJSON* default_precision_item = cJSON_GetObjectItemCaseSensitive(global_cfg, "defaultPrecision");
    if (default_precision_item) {
      if (!cJSON_IsString(default_precision_item) || !default_precision_item->valuestring) {
        _SetErrorMessage(out_error_message, "Package defaultPrecision must be a string: " + package_path);
        return false;
      }
      if (!_ParseScalarPrecisionBits(
            default_precision_item->valuestring,
            package_path,
            &default_bits,
            out_error_message)) {
        return false;
      }
    }
  }

  const cJSON* container_section = cJSON_GetObjectItemCaseSensitive(root, "container");
  if (!container_section || !cJSON_IsObject(container_section)) {
    _SetErrorMessage(out_error_message, "Package JSON is missing container section: " + package_path);
    return false;
  }
  out_schema->default_scalar_bits = default_bits;

  const auto load_named_group = [&](
                                const cJSON* group_item,
                                const char* group_name,
                                const char* default_prefix,
                                algorithm::AlgorithmContainerStorageKind storage_kind) -> bool {
    if (cJSON_IsNumber(group_item)) {
      const uint32_t count = json_utils::GetUintField(container_section, group_name);
      for (uint32_t i = 0; i < count; ++i) {
        if (!_RegisterContainerInfo(
              std::string(default_prefix) + std::to_string(i + 1u),
              default_bits,
              storage_kind,
              package_path,
              out_schema,
              out_error_message)) {
          return false;
        }
      }
      return true;
    }
    if (!cJSON_IsObject(group_item)) {
      return true;
    }
    for (const cJSON* child = group_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        _SetErrorMessage(out_error_message, "Package container name is empty: " + package_path);
        return false;
      }
      uint32_t scalar_bits = default_bits;
      if (cJSON_IsObject(child)) {
        const cJSON* precision_item = cJSON_GetObjectItemCaseSensitive(child, "precision");
        if (precision_item) {
          if (!cJSON_IsString(precision_item) || !precision_item->valuestring) {
            _SetErrorMessage(out_error_message, "Container precision must be a string: " + package_path);
            return false;
          }
          if (!_ParseScalarPrecisionBits(
                precision_item->valuestring,
                package_path,
                &scalar_bits,
                out_error_message)) {
            return false;
          }
        }
      }
      if (!_RegisterContainerInfo(
            child->string,
            scalar_bits,
            storage_kind,
            package_path,
            out_schema,
            out_error_message)) {
        return false;
      }
    }
    return true;
  };

  if (!load_named_group(
        cJSON_GetObjectItemCaseSensitive(container_section, "variable"),
        "variable",
        "v",
        algorithm::AlgorithmContainerStorageKind::TemporaryRegister) ||
      !load_named_group(
        cJSON_GetObjectItemCaseSensitive(container_section, "variableArray"),
        "variableArray",
        "a",
        algorithm::AlgorithmContainerStorageKind::Array)) {
    return false;
  }

  const cJSON* aliases = cJSON_GetObjectItemCaseSensitive(container_section, "aliases");
  if (aliases) {
    if (!cJSON_IsArray(aliases)) {
      _SetErrorMessage(out_error_message, "Package container aliases must be an array: " + package_path);
      return false;
    }

    const auto resolve_single_container_name = [&](
                                                 const std::string& token,
                                                 std::string* out_container_name) -> bool {
      if (!out_container_name) {
        _SetErrorMessage(out_error_message, "Package alias target output pointer is null.");
        return false;
      }
      out_container_name->clear();
      if (out_schema->container_infos.find(token) != out_schema->container_infos.end()) {
        *out_container_name = token;
        return true;
      }
      return false;
    };

    const int alias_count = cJSON_GetArraySize(aliases);
    for (int i = 0; i < alias_count; ++i) {
      const cJSON* alias_item = cJSON_GetArrayItem(aliases, i);
      if (!alias_item || !cJSON_IsString(alias_item) || !alias_item->valuestring) {
        _SetErrorMessage(out_error_message, "Package container alias entry must be a string: " + package_path);
        return false;
      }

      const std::string alias_text = alias_item->valuestring;
      const std::string::size_type colon = alias_text.find(':');
      if (colon == std::string::npos || alias_text.find(':', colon + 1u) != std::string::npos) {
        _SetErrorMessage(
          out_error_message,
          "Package container alias must use the form name1,name2:alias: " + alias_text +
            " (" + package_path + ")");
        return false;
      }

      const std::string alias_name = [&]() {
        std::string value = alias_text.substr(colon + 1u);
        const auto is_space = [](unsigned char ch) {
          return std::isspace(ch) != 0;
        };
        const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
        const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
        return begin >= end ? std::string{} : std::string(begin, end);
      }();
      if (alias_name.empty()) {
        _SetErrorMessage(out_error_message, "Package container alias name is empty: " + package_path);
        return false;
      }
      if (out_schema->container_infos.find(alias_name) != out_schema->container_infos.end() ||
          out_schema->container_aliases_by_name.find(alias_name) != out_schema->container_aliases_by_name.end()) {
        _SetErrorMessage(
          out_error_message,
          "Package container alias conflicts with an existing container or alias name: " + alias_name +
            " (" + package_path + ")");
        return false;
      }

      std::vector<std::string> resolved_container_names{};
      std::unordered_set<std::string> unique_container_names{};
      const std::string source_text = alias_text.substr(0, colon);
      size_t cursor = 0u;
      while (cursor <= source_text.size()) {
        const std::string::size_type comma = source_text.find(',', cursor);
        std::string token = source_text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
        const auto is_space = [](unsigned char ch) {
          return std::isspace(ch) != 0;
        };
        const auto begin = std::find_if_not(token.begin(), token.end(), is_space);
        const auto end = std::find_if_not(token.rbegin(), token.rend(), is_space).base();
        token = begin >= end ? std::string{} : std::string(begin, end);
        if (token.empty()) {
          _SetErrorMessage(
            out_error_message,
            "Package container alias contains an empty source name: " + alias_text +
              " (" + package_path + ")");
          return false;
        }

        std::string resolved_name{};
        if (!resolve_single_container_name(token, &resolved_name)) {
          _SetErrorMessage(
            out_error_message,
            "Package container alias references an unknown container name: " + token +
              " (" + package_path + ")");
          return false;
        }
        if (!unique_container_names.insert(resolved_name).second) {
          _SetErrorMessage(
            out_error_message,
            "Package container alias repeats the same container more than once: " + resolved_name +
              " (" + package_path + ")");
          return false;
        }
        resolved_container_names.push_back(std::move(resolved_name));

        if (comma == std::string::npos) {
          break;
        }
        cursor = comma + 1u;
      }

      if (resolved_container_names.empty()) {
        _SetErrorMessage(out_error_message, "Package container alias has no source containers: " + package_path);
        return false;
      }
      out_schema->container_aliases_by_name.emplace(alias_name, std::move(resolved_container_names));
    }
  }

  return true;
}

std::string _TrimText(std::string value) {
  const auto is_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };
  const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  return begin >= end ? std::string{} : std::string(begin, end);
}

bool _TryParsePackedSegmentBitsToken(
  std::string_view text,
  uint32_t* out_bits) {
  if (!out_bits || text.empty()) {
    return false;
  }
  uint64_t bits = 0u;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    bits = bits * 10u + static_cast<uint64_t>(ch - '0');
    if (bits > UINT32_MAX) {
      return false;
    }
  }
  if (bits == 0u) {
    return false;
  }
  *out_bits = static_cast<uint32_t>(bits);
  return true;
}

bool _ParsePackedSplitPrecisions(
  const cJSON* binding_item,
  const PackageDecomposerSchema& schema,
  size_t target_count,
  const std::string& package_path,
  std::vector<uint32_t>* out_segment_bits,
  std::string* out_error_message) {
  if (!out_segment_bits) {
    _SetErrorMessage(out_error_message, "Packed segment bit output vector is null.");
    return false;
  }
  out_segment_bits->clear();

  const cJSON* pricise_item = cJSON_GetObjectItemCaseSensitive(binding_item, "pricise");
  if (!pricise_item) {
    return true;
  }
  if (target_count == 0u) {
    _SetErrorMessage(out_error_message, "Packed descriptor split requires at least one target: " + package_path);
    return false;
  }

  auto append_bits = [&](uint32_t bits) {
    if (bits == 0u) {
      _SetErrorMessage(out_error_message, "Packed descriptor split bit width must be positive: " + package_path);
      return false;
    }
    out_segment_bits->push_back(bits);
    return true;
  };

  if (cJSON_IsArray(pricise_item)) {
    const int item_count = cJSON_GetArraySize(pricise_item);
    for (int i = 0; i < item_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(pricise_item, i);
      uint32_t bits = 0u;
      std::string replacement{};
      if (item && cJSON_IsNumber(item) &&
          std::isfinite(item->valuedouble) &&
          std::trunc(item->valuedouble) == item->valuedouble &&
          item->valuedouble > 0.0 &&
          item->valuedouble <= static_cast<double>(UINT32_MAX)) {
        bits = static_cast<uint32_t>(item->valuedouble);
      } else if (item && cJSON_IsString(item) && item->valuestring &&
                 (json_utils::TryEvaluatePriciseArrayToken(
                    _TrimText(item->valuestring),
                    schema.default_scalar_bits,
                    &replacement)
                    ? _TryParsePackedSegmentBitsToken(replacement, &bits)
                    : _TryParsePackedSegmentBitsToken(_TrimText(item->valuestring), &bits))) {
      } else {
        _SetErrorMessage(out_error_message, "Packed descriptor split bits must be positive integers: " + package_path);
        return false;
      }
      if (!append_bits(bits)) {
        return false;
      }
    }
  } else if (cJSON_IsString(pricise_item) && pricise_item->valuestring) {
    const std::string text = _TrimText(pricise_item->valuestring);
    const std::string::size_type slash = text.rfind('/');
    if (slash != std::string::npos) {
      const std::string prefix = _TrimText(text.substr(0, slash));
      const std::string divisor_text = _TrimText(text.substr(slash + 1u));
      std::string normalized_prefix{};
      normalized_prefix.reserve(prefix.size());
      for (char ch : prefix) {
        normalized_prefix.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      }
      if (!prefix.empty() &&
          normalized_prefix != "pricise" &&
          normalized_prefix != "precision") {
        _SetErrorMessage(out_error_message, "Packed descriptor split shorthand is invalid: " + text + " (" + package_path + ")");
        return false;
      }
      uint32_t divisor = 0u;
      if (!_TryParsePackedSegmentBitsToken(divisor_text, &divisor)) {
        _SetErrorMessage(out_error_message, "Packed descriptor split divisor is invalid: " + text + " (" + package_path + ")");
        return false;
      }
      if (divisor != target_count) {
        _SetErrorMessage(
          out_error_message,
          "Packed descriptor split divisor does not match target count: " + text + " (" + package_path + ")");
        return false;
      }
      if (schema.default_scalar_bits == 0u || schema.default_scalar_bits % divisor != 0u) {
        _SetErrorMessage(
          out_error_message,
          "Package defaultPrecision cannot be evenly divided by packed descriptor split divisor: " +
            text + " (" + package_path + ")");
        return false;
      }
      const uint32_t split_bits = schema.default_scalar_bits / divisor;
      for (uint32_t i = 0u; i < divisor; ++i) {
        if (!append_bits(split_bits)) {
          return false;
        }
      }
    } else {
      size_t cursor = 0u;
      while (cursor <= text.size()) {
        const std::string::size_type comma = text.find(',', cursor);
        const std::string token = _TrimText(
          text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor));
        uint32_t bits = 0u;
        if (!_TryParsePackedSegmentBitsToken(token, &bits)) {
          _SetErrorMessage(out_error_message, "Packed descriptor split bit list is invalid: " + text + " (" + package_path + ")");
          return false;
        }
        if (!append_bits(bits)) {
          return false;
        }
        if (comma == std::string::npos) {
          break;
        }
        cursor = comma + 1u;
      }
    }
  } else {
    _SetErrorMessage(out_error_message, "Packed descriptor split must be an array or string: " + package_path);
    return false;
  }

  if (out_segment_bits->size() != target_count) {
    _SetErrorMessage(out_error_message, "Packed descriptor split count does not match target count: " + package_path);
    return false;
  }
  return true;
}

const PackageDecomposerContainerInfo* _FindContainerInfo(
  const PackageDecomposerSchema& schema,
  std::string_view container_name) {
  const auto found = schema.container_infos.find(std::string(container_name));
  return found == schema.container_infos.end() ? nullptr : &found->second;
}

bool _TryResolveTargetContainerNames(
  const PackageDecomposerSchema& schema,
  std::string_view container_name,
  std::vector<std::string>* out_container_names) {
  if (!out_container_names) {
    return false;
  }
  out_container_names->clear();
  if (container_name.empty()) {
    return false;
  }
  if (_FindContainerInfo(schema, container_name)) {
    out_container_names->push_back(std::string(container_name));
    return true;
  }
  const auto alias_found = schema.container_aliases_by_name.find(std::string(container_name));
  if (alias_found == schema.container_aliases_by_name.end()) {
    return false;
  }
  *out_container_names = alias_found->second;
  return !out_container_names->empty();
}

bool _ResolveSingleTargetContainerName(
  const PackageDecomposerSchema& schema,
  std::string_view container_name,
  std::string* out_container_name) {
  if (!out_container_name) {
    return false;
  }
  out_container_name->clear();
  std::vector<std::string> resolved_names{};
  if (!_TryResolveTargetContainerNames(schema, container_name, &resolved_names) ||
      resolved_names.size() != 1u) {
    return false;
  }
  *out_container_name = resolved_names.front();
  return true;
}

bool _ExpandTargetContainerNameList(
  const PackageDecomposerSchema& schema,
  const std::vector<std::string>& raw_names,
  const std::string& package_path,
  const std::string& field_name,
  std::vector<std::string>* out_container_names,
  std::string* out_error_message) {
  if (!out_container_names) {
    _SetErrorMessage(out_error_message, "Target container expansion output vector is null.");
    return false;
  }
  out_container_names->clear();
  for (const std::string& raw_name : raw_names) {
    std::vector<std::string> resolved_names{};
    if (!_TryResolveTargetContainerNames(schema, raw_name, &resolved_names)) {
      _SetErrorMessage(
        out_error_message,
        "Package " + field_name + " references an unknown target container '" + raw_name +
          "': " + package_path);
      return false;
    }
    out_container_names->insert(out_container_names->end(), resolved_names.begin(), resolved_names.end());
  }
  if (out_container_names->empty()) {
    _SetErrorMessage(
      out_error_message,
      "Package " + field_name + " resolves to an empty target container list: " + package_path);
    return false;
  }
  return true;
}

bool _TryResolvePackedSourceScalarBits(
  const PackageDecomposerSchema& schema,
  std::string_view source_name,
  uint32_t* out_source_scalar_bits,
  std::string* out_error_message) {
  if (!out_source_scalar_bits) {
    _SetErrorMessage(out_error_message, "Packed descriptor source precision output pointer is null.");
    return false;
  }

  *out_source_scalar_bits = schema.default_scalar_bits;
  if (source_name.empty()) {
    return true;
  }

  std::vector<std::string> resolved_container_names{};
  if (!_TryResolveTargetContainerNames(schema, source_name, &resolved_container_names)) {
    return true;
  }
  if (resolved_container_names.empty()) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor source resolved to an empty container list: " + std::string(source_name));
    return false;
  }

  uint32_t total_bits = 0u;
  for (const std::string& container_name : resolved_container_names) {
    const PackageDecomposerContainerInfo* container_info = _FindContainerInfo(schema, container_name);
    if (!container_info) {
      _SetErrorMessage(
        out_error_message,
        "Packed descriptor source container schema is missing for '" + container_name + "'.");
      return false;
    }
    if (container_info->scalar_bits == 0u) {
      _SetErrorMessage(
        out_error_message,
        "Packed descriptor source container has invalid zero-bit precision: " + container_name);
      return false;
    }
    if (total_bits > UINT32_MAX - container_info->scalar_bits) {
      _SetErrorMessage(
        out_error_message,
        "Packed descriptor source precision overflowed while resolving '" +
          std::string(source_name) + "'.");
      return false;
    }
    total_bits += container_info->scalar_bits;
  }

  *out_source_scalar_bits = total_bits;
  return true;
}

bool _ValidatePackedSplitSourcePrecision(
  const PackageDecomposerSchema& schema,
  const std::string& package_path,
  PackageDecomposerDescriptionBinding* binding,
  std::string* out_error_message) {
  if (!binding) {
    _SetErrorMessage(out_error_message, "Packed descriptor binding output pointer is null.");
    return false;
  }
  binding->source_scalar_bits = 0u;
  if (binding->packed_segment_bits.empty()) {
    return true;
  }

  uint32_t source_scalar_bits = 0u;
  if (!_TryResolvePackedSourceScalarBits(
        schema,
        binding->from_names.front(),
        &source_scalar_bits,
        out_error_message)) {
    return false;
  }
  if (source_scalar_bits == 0u) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor source precision must be positive for '" + binding->from_names.front() +
        "' in " + package_path);
    return false;
  }

  uint32_t total_bits = 0u;
  for (uint32_t bits : binding->packed_segment_bits) {
    if (total_bits > UINT32_MAX - bits) {
      _SetErrorMessage(
        out_error_message,
        "Packed descriptor split bit width sum overflowed for '" + binding->from_names.front() +
          "' in " + package_path);
      return false;
    }
    total_bits += bits;
  }
  if (total_bits > source_scalar_bits) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor split declares " + std::to_string(total_bits) +
        " bits, but source '" + binding->from_names.front() + "' only provides " +
        std::to_string(source_scalar_bits) + " bits in " + package_path);
    return false;
  }

  binding->source_scalar_bits = source_scalar_bits;
  return true;
}

bool _ParsePackageDecomposerBinding(
  const cJSON* binding_item,
  const PackageDecomposerSchema& schema,
  const std::string& package_path,
  PackageDecomposerDescriptionBinding* out_binding,
  std::string* out_error_message) {
  if (!binding_item || !cJSON_IsObject(binding_item) || !out_binding) {
    _SetErrorMessage(out_error_message, "Invalid description binding in package JSON: " + package_path);
    return false;
  }

  out_binding->from_names = json_utils::GetStringList(cJSON_GetObjectItemCaseSensitive(binding_item, "from"));
  const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(binding_item, "to");
  if (to_item && cJSON_IsObject(to_item)) {
    const std::string raw_container_name = json_utils::GetStringField(to_item, "container");
    if (!raw_container_name.empty() &&
        !_ResolveSingleTargetContainerName(schema, raw_container_name, &out_binding->target_container_name)) {
      _SetErrorMessage(
        out_error_message,
        "Description binding target container must resolve to exactly one container: " +
          raw_container_name + " (" + package_path + ")");
      return false;
    }
    out_binding->target_indices = json_utils::GetUintList(cJSON_GetObjectItemCaseSensitive(to_item, "indices"));
  } else {
    if (!_ExpandTargetContainerNameList(
          schema,
          json_utils::GetStringList(to_item),
          package_path,
          "decomposer.to",
          &out_binding->target_container_names,
          out_error_message)) {
      return false;
    }
  }
  const std::string codec_text = [&]() -> std::string {
    const std::string value_codec = json_utils::GetStringField(binding_item, "valueCodec");
    if (!value_codec.empty()) {
      return value_codec;
    }
    const std::string codec = json_utils::GetStringField(binding_item, "codec");
    if (!codec.empty()) {
      return codec;
    }
    return json_utils::GetStringField(binding_item, "decodeAs");
  }();
  if (!codec_text.empty() && !_NormalizeDescriptorValueCodec(codec_text, &out_binding->value_codec)) {
    _SetErrorMessage(
      out_error_message,
      "Unsupported descriptor codec '" + codec_text + "' in package JSON: " + package_path);
    return false;
  }

  const size_t target_count = !out_binding->target_container_names.empty()
    ? out_binding->target_container_names.size()
    : out_binding->target_indices.size();
  if (!_ParsePackedSplitPrecisions(
        binding_item,
        schema,
        target_count,
        package_path,
        &out_binding->packed_segment_bits,
        out_error_message)) {
    return false;
  }

  if (out_binding->from_names.empty()) {
    _SetErrorMessage(out_error_message, "Description binding is missing source names: " + package_path);
    return false;
  }
  if (out_binding->target_container_name.empty() &&
      out_binding->target_container_names.empty() &&
      out_binding->target_indices.empty()) {
    _SetErrorMessage(out_error_message, "Description binding is missing targets: " + package_path);
    return false;
  }
  if (!out_binding->packed_segment_bits.empty() && out_binding->from_names.size() != 1u) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor split requires exactly one source descriptor: " + package_path);
    return false;
  }
  if (!_ValidatePackedSplitSourcePrecision(
        schema,
        package_path,
        out_binding,
        out_error_message)) {
    return false;
  }
  return true;
}

bool _ParsePackageDecomposerDescriptionEntry(
  const cJSON* entry_item,
  const PackageDecomposerSchema& schema,
  const std::string& package_path,
  PackageDecomposerDescriptionEntry* out_entry,
  std::string* out_error_message) {
  if (!entry_item || !cJSON_IsObject(entry_item) || !out_entry) {
    _SetErrorMessage(out_error_message, "Invalid description entry in package JSON: " + package_path);
    return false;
  }

  out_entry->name = json_utils::GetStringField(entry_item, "name");
  if (out_entry->name.empty()) {
    _SetErrorMessage(out_error_message, "Description entry is missing a name: " + package_path);
    return false;
  }

  PackageDecomposerDescriptionBinding binding{};
  if (!_ParsePackageDecomposerBinding(entry_item, schema, package_path, &binding, out_error_message)) {
    return false;
  }
  out_entry->bindings.push_back(std::move(binding));
  return true;
}

bool _ParsePackageDecomposerResourceGroup(
  const cJSON* group_item,
  const std::string& resource_kind,
  const std::string& package_path,
  PackageDecomposerSchema* out_schema,
  std::string* out_error_message) {
  if (!group_item || !cJSON_IsObject(group_item) || !out_schema) {
    _SetErrorMessage(out_error_message, "Invalid resource group in package JSON: " + package_path);
    return false;
  }

  for (const cJSON* child = group_item->child; child; child = child->next) {
    if (!child->string || !*child->string || !cJSON_IsString(child) || !child->valuestring) {
      _SetErrorMessage(out_error_message, "Invalid resource binding in package JSON: " + package_path);
      return false;
    }

    std::string container_name{};
    if (!_ResolveSingleTargetContainerName(*out_schema, child->valuestring, &container_name)) {
      _SetErrorMessage(
        out_error_message,
        "Resource binding target container must resolve to exactly one container: " +
          std::string(child->valuestring) + " (" + package_path + ")");
      return false;
    }
    out_schema->mesh_bindings.push_back(PackageDecomposerMeshBinding{
      .resource_kind = resource_kind,
      .resource_name = child->string,
      .container_name = std::move(container_name),
    });
  }
  return true;
}

PackageDecomposerSchema _LoadPackageDecomposerSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  PackageDecomposerSchema schema{};
  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    schema.error_message = "Failed to resolve package JSON file for decomposer.";
    return schema;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    return schema;
  }

  if (!_LoadContainerInfos(root, package_json_path.generic_string(), &schema, &schema.error_message)) {
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* decomposer = cJSON_GetObjectItemCaseSensitive(root, "decomposer");
  if (!decomposer || !cJSON_IsObject(decomposer)) {
    schema.error_message = "Decomposer JSON file is missing a 'decomposer' object: " + package_json_path.generic_string();
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* res = cJSON_GetObjectItemCaseSensitive(decomposer, "res");
  if (!res || !cJSON_IsObject(res)) {
    schema.error_message = "Decomposer JSON file is missing a 'res' object: " + package_json_path.generic_string();
    cJSON_Delete(root);
    return schema;
  }
  for (const cJSON* kind_item = res->child; kind_item; kind_item = kind_item->next) {
    if (!kind_item->string || !*kind_item->string) {
      schema.error_message = "Invalid resource group in package JSON: " + package_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    if (!cJSON_IsObject(kind_item)) {
      schema.error_message = "Invalid resource group payload in package JSON: " + package_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    if (!_ParsePackageDecomposerResourceGroup(
          kind_item,
          kind_item->string,
          package_json_path.generic_string(),
          &schema,
          &schema.error_message)) {
      cJSON_Delete(root);
      return schema;
    }
  }

  const cJSON* description = cJSON_GetObjectItemCaseSensitive(decomposer, "description");
  if (description && cJSON_IsArray(description)) {
    const int description_count = cJSON_GetArraySize(description);
    schema.description_entries.reserve(description_count > 0 ? static_cast<size_t>(description_count) : 0u);
    for (int i = 0; i < description_count; ++i) {
      const cJSON* entry_item = cJSON_GetArrayItem(description, i);
      PackageDecomposerDescriptionEntry entry{};
      if (!_ParsePackageDecomposerDescriptionEntry(
            entry_item,
            schema,
            package_json_path.generic_string(),
            &entry,
            &schema.error_message)) {
        cJSON_Delete(root);
        return schema;
      }
      schema.description_entries.push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  schema.valid = true;
  return schema;
}

bool _WriteFloatValue(
  algorithm::AlgorithmContainer* container,
  float value) {
  if (!container) {
    return false;
  }
  if (container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data(), &value, sizeof(float));
  return true;
}

bool _WriteFloatValueAtIndex(
  algorithm::AlgorithmContainer* container,
  uint32_t index,
  float value) {
  if (!container) {
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container->element_stride);
  if (container->bytes.size() < byte_offset + sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, &value, sizeof(float));
  return true;
}

const agent::AlgorithmDescriptorValue* _FindDescriptorValue(
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
  std::string_view descriptor_name) {
  for (const agent::AlgorithmDescriptorValue& value : descriptor_values) {
    if (value.descriptor_name == descriptor_name) {
      return &value;
    }
  }
  return nullptr;
}

bool _ExtractPackedDescriptorSegments(
  const agent::AlgorithmDescriptorValue& descriptor_value,
  uint32_t source_scalar_bits,
  const std::vector<uint32_t>& packed_segment_bits,
  std::vector<double>* out_segment_values,
  std::string* out_error_message) {
  if (!out_segment_values) {
    _SetErrorMessage(out_error_message, "Packed descriptor segment output vector is null.");
    return false;
  }
  out_segment_values->clear();
  if (packed_segment_bits.empty()) {
    return true;
  }
  if (!std::isfinite(descriptor_value.scalar_value) ||
      std::trunc(descriptor_value.scalar_value) != descriptor_value.scalar_value) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor source '" + descriptor_value.descriptor_name + "' must be an exact integer value.");
    return false;
  }
  if (descriptor_value.scalar_value < 0.0) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor source '" + descriptor_value.descriptor_name + "' cannot be negative.");
    return false;
  }

  uint32_t total_bits = 0u;
  for (uint32_t bits : packed_segment_bits) {
    if (bits == 0u) {
      _SetErrorMessage(out_error_message, "Packed descriptor split bit width must be positive.");
      return false;
    }
    if (total_bits > UINT32_MAX - bits) {
      _SetErrorMessage(out_error_message, "Packed descriptor split bit width sum overflowed.");
      return false;
    }
    total_bits += bits;
  }
  if (total_bits == 0u) {
    _SetErrorMessage(out_error_message, "Packed descriptor split bit width sum must be positive.");
    return false;
  }
  if (source_scalar_bits != 0u && total_bits > source_scalar_bits) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor split bit width sum exceeds the declared source precision.");
    return false;
  }
  if (total_bits > 64u) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor split currently supports at most 64 source bits.");
    return false;
  }

  const double max_exact_value = std::ldexp(1.0, static_cast<int>(total_bits)) - 1.0;
  if (descriptor_value.scalar_value > max_exact_value) {
    _SetErrorMessage(
      out_error_message,
      "Packed descriptor source '" + descriptor_value.descriptor_name + "' exceeds the declared packed bit width.");
    return false;
  }

  const uint64_t raw_value = static_cast<uint64_t>(descriptor_value.scalar_value);
  out_segment_values->reserve(packed_segment_bits.size());
  uint32_t remaining_bits = total_bits;
  for (uint32_t bits : packed_segment_bits) {
    remaining_bits -= bits;
    const uint64_t mask = bits >= 64u ? std::numeric_limits<uint64_t>::max() : ((1ull << bits) - 1ull);
    const uint64_t segment_value = (raw_value >> remaining_bits) & mask;
    out_segment_values->push_back(static_cast<double>(segment_value));
  }
  return true;
}

const agent::AlgorithmResourceBinding* _FindResourceBinding(
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  std::string_view resource_name) {
  for (const agent::AlgorithmResourceBinding& binding : resource_bindings) {
    if (binding.resource_name == resource_name) {
      return &binding;
    }
  }
  return nullptr;
}

class PackageResourceDecomposer final {
 public:
  explicit PackageResourceDecomposer(const PackageDecomposerSchema& schema)
    : schema_(schema) {}

  bool GetRequestedResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    algorithmManager::AlgorithmRequestedResources* out_requested_resources) const {
    if (!out_requested_resources) {
      return false;
    }
    *out_requested_resources = {};
    out_requested_resources->algorithm_name = algorithm_profile.algorithm_name;
    out_requested_resources->required_resources.reserve(schema_.mesh_bindings.size());
    for (const PackageDecomposerMeshBinding& binding : schema_.mesh_bindings) {
      out_requested_resources->required_resources.push_back(agent::AlgorithmRequestedResources::RequiredResource{
        .resource_name = binding.resource_name,
        .resource_kind = binding.resource_kind,
        .required = true,
      });
    }
    out_requested_resources->valid = true;
    return true;
  }

  bool DecomposeResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<algorithmManager::AlgorithmResourceBinding>& resource_bindings,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    if (!container_set) {
      _SetErrorMessage(out_error_message, "AlgorithmContainerSet output pointer is null.");
      return false;
    }

    for (const PackageDecomposerMeshBinding& binding : schema_.mesh_bindings) {
      const agent::AlgorithmResourceBinding* resource_binding =
        _FindResourceBinding(resource_bindings, binding.resource_name);
      if (!resource_binding) {
        _SetErrorMessage(
          out_error_message,
          "Missing required resource binding '" + binding.resource_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (resource_binding->source_path.empty()) {
        _SetErrorMessage(
          out_error_message,
          "Required resource '" + binding.resource_name + "' has no source path for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }

      std::ifstream file(resource_binding->source_path, std::ios::binary);
      if (!file) {
        _SetErrorMessage(
          out_error_message,
          "Required resource file '" + resource_binding->source_path + "' could not be opened for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }

      algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.container_name);
      if (!container) {
        _SetErrorMessage(
          out_error_message,
          "Missing target container '" + binding.container_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      std::string file_text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      const size_t copy_size = std::min(container->bytes.size(), file_text.size());
      if (copy_size > 0u) {
        std::memcpy(container->bytes.data(), file_text.data(), copy_size);
      }
    }

    return true;
  }

 private:
  const PackageDecomposerSchema& schema_;
};

class PackageDescriptorDecomposer final {
 public:
  explicit PackageDescriptorDecomposer(const PackageDecomposerSchema& schema)
    : schema_(schema) {}

  bool GetRequestedDescriptorBindings(
    const algorithm::AlgorithmProfile& algorithm_profile,
    algorithmManager::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    if (!out_requested_descriptor_bindings) {
      return false;
    }
    *out_requested_descriptor_bindings = {};
    out_requested_descriptor_bindings->algorithm_name = algorithm_profile.algorithm_name;

    for (const PackageDecomposerDescriptionEntry& entry : schema_.description_entries) {
      for (const PackageDecomposerDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          if (binding.from_names.size() != binding.target_container_names.size() &&
              binding.from_names.size() != 1u) {
            return false;
          }
          for (size_t i = 0; i < binding.target_container_names.size(); ++i) {
            const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
            const PackageDecomposerContainerInfo* container_info =
              _FindContainerInfo(schema_, binding.target_container_names[i]);
            if (!container_info) {
              return false;
            }
            out_requested_descriptor_bindings->descriptor_slots.push_back(
              agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
                .descriptor_name = binding.from_names[from_index],
                .container_name = binding.target_container_names[i],
                .array_index = 0u,
              });
          }
          continue;
        }

        if (binding.from_names.size() != binding.target_indices.size() &&
            binding.from_names.size() != 1u) {
          return false;
        }
        for (size_t i = 0; i < binding.target_indices.size(); ++i) {
          const uint32_t target_index = binding.target_indices[i];
          if (target_index == 0u) {
            return false;
          }
          const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
          const PackageDecomposerContainerInfo* container_info =
            _FindContainerInfo(schema_, binding.target_container_name);
          if (!container_info) {
            return false;
          }
          out_requested_descriptor_bindings->descriptor_slots.push_back(
            agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
              .descriptor_name = binding.from_names[from_index],
              .container_name = binding.target_container_name,
              .array_index = target_index - 1u,
            });
        }
      }
    }

    out_requested_descriptor_bindings->valid = true;
    return true;
  }

  bool DecomposeDescriptors(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<algorithmManager::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    if (!container_set) {
      _SetErrorMessage(out_error_message, "AlgorithmContainerSet output pointer is null.");
      return false;
    }

    for (const PackageDecomposerDescriptionEntry& entry : schema_.description_entries) {
      for (const PackageDecomposerDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          if (!WriteDescriptorTargetsByName(
                algorithm_profile,
                entry,
                binding,
                descriptor_values,
                container_set,
                out_error_message)) {
            return false;
          }
          continue;
        }

        if (!WriteDescriptorTargetsByIndex(
              algorithm_profile,
              entry,
              binding,
              descriptor_values,
              container_set,
              out_error_message)) {
          return false;
        }
      }
    }

    return true;
  }

 private:
  bool WriteDescriptorTargetsByName(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const PackageDecomposerDescriptionEntry& entry,
    const PackageDecomposerDescriptionBinding& binding,
    const std::vector<algorithmManager::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    const size_t from_count = binding.from_names.size();
    const size_t target_count = binding.target_container_names.size();
    if (from_count != target_count && from_count != 1u) {
      _SetErrorMessage(
        out_error_message,
        "Description entry '" + entry.name + "' has mismatched from/to list sizes for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }

    std::vector<double> packed_segment_values{};
    if (!PreparePackedSegmentValues(
          algorithm_profile,
          binding,
          descriptor_values,
          target_count,
          "target count",
          &packed_segment_values,
          out_error_message)) {
      return false;
    }

    for (size_t i = 0; i < target_count; ++i) {
      const size_t from_index = from_count == 1u ? 0u : i;
      const agent::AlgorithmDescriptorValue* value =
        binding.packed_segment_bits.empty()
          ? _FindDescriptorValue(descriptor_values, binding.from_names[from_index])
          : nullptr;
      if (binding.packed_segment_bits.empty() && !value) {
        _SetErrorMessage(
          out_error_message,
          "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      const double value_to_write =
        binding.packed_segment_bits.empty()
          ? value->scalar_value
          : packed_segment_values[i];
      const std::string write_codec =
        binding.packed_segment_bits.empty() ? binding.value_codec : std::string("uint");

      algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_names[i]);
      if (!container) {
        _SetErrorMessage(
          out_error_message,
          "Missing target container '" + binding.target_container_names[i] + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      const PackageDecomposerContainerInfo* container_info =
        _FindContainerInfo(schema_, binding.target_container_names[i]);
      if (!container_info) {
        _SetErrorMessage(
          out_error_message,
          "Missing target container schema '" + binding.target_container_names[i] + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (!_WriteDescriptorScalarValue(
            container,
            0u,
            container_info->scalar_bits,
            write_codec,
            value_to_write,
            out_error_message)) {
        if (out_error_message && out_error_message->empty()) {
          _SetErrorMessage(
            out_error_message,
            "Target container '" + binding.target_container_names[i] + "' could not accept descriptor value for '" +
              algorithm_profile.algorithm_name + "'.");
        }
        return false;
      }
    }

    return true;
  }

  bool WriteDescriptorTargetsByIndex(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const PackageDecomposerDescriptionEntry& entry,
    const PackageDecomposerDescriptionBinding& binding,
    const std::vector<algorithmManager::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    const size_t from_count = binding.from_names.size();
    const size_t target_count = binding.target_indices.size();
    if (from_count != target_count && from_count != 1u) {
      _SetErrorMessage(
        out_error_message,
        "Description entry '" + entry.name + "' has mismatched from/to list sizes for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }

    std::vector<double> packed_segment_values{};
    if (!PreparePackedSegmentValues(
          algorithm_profile,
          binding,
          descriptor_values,
          target_count,
          "array target count",
          &packed_segment_values,
          out_error_message)) {
      return false;
    }

    algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_name);
    if (!container) {
      _SetErrorMessage(
        out_error_message,
        "Missing target container '" + binding.target_container_name + "' for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }
    const PackageDecomposerContainerInfo* container_info =
      _FindContainerInfo(schema_, binding.target_container_name);
    if (!container_info) {
      _SetErrorMessage(
        out_error_message,
        "Missing target container schema '" + binding.target_container_name + "' for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }
    if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
      _SetErrorMessage(
        out_error_message,
        "Target container '" + binding.target_container_name + "' is not an array for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }

    for (size_t i = 0; i < target_count; ++i) {
      const uint32_t target_index = binding.target_indices[i];
      const size_t from_index = from_count == 1u ? 0u : i;
      const agent::AlgorithmDescriptorValue* value =
        binding.packed_segment_bits.empty()
          ? _FindDescriptorValue(descriptor_values, binding.from_names[from_index])
          : nullptr;
      if (binding.packed_segment_bits.empty() && !value) {
        _SetErrorMessage(
          out_error_message,
          "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      const double value_to_write =
        binding.packed_segment_bits.empty()
          ? value->scalar_value
          : packed_segment_values[i];
      const std::string write_codec =
        binding.packed_segment_bits.empty() ? binding.value_codec : std::string("uint");
      if (target_index == 0u) {
        _SetErrorMessage(
          out_error_message,
          "Unsupported descriptor mapping '" + binding.from_names[from_index] + "' for container '" +
            binding.target_container_name + "'.");
        return false;
      }

      const size_t byte_offset =
        static_cast<size_t>(target_index - 1u) * static_cast<size_t>(container->element_stride);
      if (!_WriteDescriptorScalarValue(
            container,
            byte_offset,
            container_info->scalar_bits,
            write_codec,
            value_to_write,
            out_error_message)) {
        if (out_error_message && out_error_message->empty()) {
          _SetErrorMessage(
            out_error_message,
            "Target container '" + binding.target_container_name + "' could not accept descriptor value for '" +
              algorithm_profile.algorithm_name + "'.");
        }
        return false;
      }
    }

    return true;
  }

  bool PreparePackedSegmentValues(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const PackageDecomposerDescriptionBinding& binding,
    const std::vector<algorithmManager::AlgorithmDescriptorValue>& descriptor_values,
    size_t target_count,
    const std::string& target_count_label,
    std::vector<double>* out_packed_segment_values,
    std::string* out_error_message) const {
    if (!out_packed_segment_values) {
      _SetErrorMessage(out_error_message, "Packed descriptor segment output vector is null.");
      return false;
    }
    out_packed_segment_values->clear();
    if (binding.packed_segment_bits.empty()) {
      return true;
    }

    const agent::AlgorithmDescriptorValue* source_value =
      _FindDescriptorValue(descriptor_values, binding.from_names.front());
    if (!source_value) {
      _SetErrorMessage(
        out_error_message,
        "Missing packed descriptor value '" + binding.from_names.front() + "' for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }
    if (!_ExtractPackedDescriptorSegments(
          *source_value,
          binding.source_scalar_bits,
          binding.packed_segment_bits,
          out_packed_segment_values,
          out_error_message)) {
      return false;
    }
    if (out_packed_segment_values->size() != target_count) {
      _SetErrorMessage(
        out_error_message,
        "Packed descriptor split output count does not match " + target_count_label + " for '" +
          algorithm_profile.algorithm_name + "'.");
      return false;
    }
    return true;
  }

  const PackageDecomposerSchema& schema_;
};

class PackageSchemaDecomposer final {
 public:
  explicit PackageSchemaDecomposer(const algorithm::AlgorithmPackageLocation& package_location)
    : schema_(_LoadPackageDecomposerSchema(package_location)),
      resource_decomposer_(schema_),
      descriptor_decomposer_(schema_) {}

  bool valid() const { return schema_.valid; }
  const std::string& error_message() const { return schema_.error_message; }

  bool GetRequestedResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    algorithmManager::AlgorithmRequestedResources* out_requested_resources) const {
    return schema_.valid &&
      resource_decomposer_.GetRequestedResources(algorithm_profile, out_requested_resources);
  }

  bool GetRequestedDescriptorBindings(
    const algorithm::AlgorithmProfile& algorithm_profile,
    algorithmManager::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    return schema_.valid &&
      descriptor_decomposer_.GetRequestedDescriptorBindings(algorithm_profile, out_requested_descriptor_bindings);
  }

  bool Decompose(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<algorithmManager::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<algorithmManager::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    if (!container_set) {
      _SetErrorMessage(out_error_message, "AlgorithmContainerSet output pointer is null.");
      return false;
    }
    if (!schema_.valid) {
      _SetErrorMessage(out_error_message, schema_.error_message);
      return false;
    }
    return resource_decomposer_.DecomposeResources(
             algorithm_profile,
             resource_bindings,
             container_set,
             out_error_message) &&
      descriptor_decomposer_.DecomposeDescriptors(
        algorithm_profile,
        descriptor_values,
        container_set,
        out_error_message);
  }

 private:
  PackageDecomposerSchema schema_{};
  PackageResourceDecomposer resource_decomposer_;
  PackageDescriptorDecomposer descriptor_decomposer_;
};

bool _CopyRuntimeMappedContainer(
  const algorithm::AlgorithmContainerSet& source_container_set,
  const algorithm::AlgorithmContainer& source_container,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algorithm::AlgorithmContainer* target_container,
  const std::string& source_stage_name,
  const std::string& target_stage_name,
  const std::string& source_container_name,
  const std::string& target_container_name,
  std::string* out_error_message) {
  if (!target_container) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer target container is null for '" + source_stage_name + "->" + target_stage_name + "'.");
    return false;
  }
  if (source_container.bytes.empty() || target_container->bytes.empty()) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer container has no data for '" + source_stage_name + "->" + target_stage_name +
        "': '" + source_container_name + "' to '" + target_container_name + "'.");
    return false;
  }

  const bool source_is_standard_slot =
    algorithm::IsStandardContainerSlotName(source_container_set, source_container_name);
  const bool target_is_standard_slot =
    algorithm::IsStandardContainerSlotName(target_container_set, target_container_name);
  if (source_is_standard_slot != target_is_standard_slot) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer cannot mix standard slots with custom containers for '" +
        source_stage_name + "->" + target_stage_name + "': '" + source_container_name +
        "' to '" + target_container_name + "'.");
    return false;
  }

  if (!source_is_standard_slot && source_container_name != target_container_name) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer requires same-name custom containers for '" + source_stage_name + "->" +
        target_stage_name + "': '" + source_container_name + "' to '" + target_container_name + "'.");
    return false;
  }

  if (!algorithm::HasSameContainerStructure(source_container, *target_container)) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer container structure mismatch for '" + source_stage_name + "->" + target_stage_name +
        "': '" + source_container_name + "' to '" + target_container_name + "'.");
    return false;
  }

  std::memcpy(target_container->bytes.data(), source_container.bytes.data(), source_container.bytes.size());
  return true;
}

bool _ApplyRuntimeTransferEdgeToTarget(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const algorithm::AlgorithmRuntimeTransferEdge& edge,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::unordered_set<std::string>* written_target_names,
  std::string* out_error_message) {
  (void)transfer_map;
  if (!written_target_names) {
    _SetErrorMessage(out_error_message, "Runtime transfer target tracking set is null.");
    return false;
  }
  if (!target_container_set) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer target container set is null for stage '" + edge.target_stage_name + "'.");
    return false;
  }

  const auto source_stage_it = stage_container_sets.find(edge.source_stage_name);
  if (source_stage_it == stage_container_sets.end() || !source_stage_it->second) {
    if (edge.bindings.empty()) {
      return true;
    }
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer source stage is unavailable: " + edge.source_stage_name);
    return false;
  }

  const algorithm::AlgorithmContainerSet& source_container_set = *source_stage_it->second;
  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge.bindings) {
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, binding.from_name);
    if (!source_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer source container is missing: " + edge.source_stage_name + "." + binding.from_name);
      return false;
    }

    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, binding.to_name);
    if (!target_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is missing: " + edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    if (written_target_names->find(binding.to_name) != written_target_names->end()) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is written more than once: " +
          edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    if (!_CopyRuntimeMappedContainer(
          source_container_set,
          *source_container,
          *target_container_set,
          target_container,
          edge.source_stage_name,
          edge.target_stage_name,
          binding.from_name,
          binding.to_name,
          out_error_message)) {
      return false;
    }

    written_target_names->insert(binding.to_name);
  }

  return true;
}

bool _ApplyRuntimeTransferEdgeFromSource(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const algorithm::AlgorithmRuntimeTransferEdge& edge,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::unordered_map<std::string, std::unordered_set<std::string>>* written_target_names_by_stage,
  std::string* out_error_message) {
  (void)transfer_map;
  if (!stage_container_sets || !written_target_names_by_stage) {
    _SetErrorMessage(out_error_message, "Runtime transfer stage container set map is null.");
    return false;
  }

  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge.bindings) {
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, binding.from_name);
    if (!source_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer source container is missing: " + edge.source_stage_name + "." + binding.from_name);
      return false;
    }

    auto target_stage_it = stage_container_sets->find(edge.target_stage_name);
    if (target_stage_it == stage_container_sets->end() || !target_stage_it->second) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target stage is unavailable: " + edge.target_stage_name);
      return false;
    }

    algorithm::AlgorithmContainerSet* target_container_set = target_stage_it->second.get();
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, binding.to_name);
    if (!target_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is missing: " + edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    std::unordered_set<std::string>& written_target_names = (*written_target_names_by_stage)[edge.target_stage_name];
    if (written_target_names.find(binding.to_name) != written_target_names.end()) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is written more than once: " +
          edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    if (!_CopyRuntimeMappedContainer(
          source_container_set,
          *source_container,
          *target_container_set,
          target_container,
          edge.source_stage_name,
          edge.target_stage_name,
          binding.from_name,
          binding.to_name,
          out_error_message)) {
      return false;
    }

    written_target_names.insert(binding.to_name);
  }

  return true;
}

void _CopyBridgeDebugBindings(
  const algorithm::AlgorithmRuntimeTransferEdge* edge,
  std::vector<algorithmManager::PipelineStageBridgeDebugBinding>* out_bindings) {
  if (!out_bindings) {
    return;
  }
  out_bindings->clear();
  if (!edge) {
    return;
  }
  out_bindings->reserve(edge->bindings.size());
  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge->bindings) {
    out_bindings->push_back(algorithmManager::PipelineStageBridgeDebugBinding{
      .source_stage_name = edge->source_stage_name,
      .target_stage_name = edge->target_stage_name,
      .source_container_name = binding.from_name,
      .target_container_name = binding.to_name,
      .required = binding.required,
    });
  }
}

const algorithm::AlgorithmRuntimeTransferStageLayout* _FindRuntimeTransferStageLayout(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& stage_name) {
  return transfer_map.FindStageLayout(stage_name);
}

bool _ReadArrayScalarAt(
  const algorithm::AlgorithmContainer& container,
  uint32_t index,
  float* out_value,
  std::string* out_error_message) {
  if (!out_value) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer read output pointer is null.");
    return false;
  }
  if (container.storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer must be an array container.");
    return false;
  }
  if (container.element_stride < sizeof(float) || index >= container.element_count) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar read is out of range.");
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container.element_stride);
  if (byte_offset + sizeof(float) > container.bytes.size()) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar read exceeds container storage.");
    return false;
  }
  std::memcpy(out_value, container.bytes.data() + byte_offset, sizeof(float));
  return true;
}

bool _WriteArrayScalarAt(
  algorithm::AlgorithmContainer* container,
  uint32_t index,
  float value,
  std::string* out_error_message) {
  if (!container) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer write target is null.");
    return false;
  }
  if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer must be an array container.");
    return false;
  }
  if (container->element_stride < sizeof(float) || index >= container->element_count) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar write is out of range.");
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container->element_stride);
  if (byte_offset + sizeof(float) > container->bytes.size()) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar write exceeds container storage.");
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, &value, sizeof(float));
  return true;
}

bool _ValidateIntegerScalarValue(
  double value,
  const std::string& codec_name,
  const std::string& container_name,
  std::string* out_error_message) {
  if (!std::isfinite(value) || std::trunc(value) != value) {
    _SetErrorMessage(
      out_error_message,
      "Descriptor value for container '" + container_name + "' is not an exact " + codec_name + " scalar.");
    return false;
  }
  return true;
}

bool _WriteDescriptorScalarValue(
  algorithm::AlgorithmContainer* container,
  size_t byte_offset,
  uint32_t scalar_bits,
  const std::string& value_codec,
  double value,
  std::string* out_error_message) {
  if (!container) {
    _SetErrorMessage(out_error_message, "Descriptor write target container is null.");
    return false;
  }
  const size_t scalar_bytes = static_cast<size_t>(scalar_bits / 8u);
  if (scalar_bytes == 0u || byte_offset + scalar_bytes > container->bytes.size()) {
    _SetErrorMessage(out_error_message, "Descriptor write exceeds target container storage.");
    return false;
  }

  void* destination = container->bytes.data() + byte_offset;
  if (value_codec == "float" || value_codec == "ieee754") {
    if (scalar_bits == 32u) {
      const float encoded = static_cast<float>(value);
      std::memcpy(destination, &encoded, sizeof(encoded));
      return true;
    }
    if (scalar_bits == 64u) {
      const double encoded = static_cast<double>(value);
      std::memcpy(destination, &encoded, sizeof(encoded));
      return true;
    }
    _SetErrorMessage(
      out_error_message,
      "Float descriptor codec requires 32-bit or 64-bit target storage.");
    return false;
  }

  if (value_codec == "int") {
    if (!_ValidateIntegerScalarValue(value, "signed integer", container->name, out_error_message)) {
      return false;
    }
    const double integral_value = static_cast<double>(value);
    switch (scalar_bits) {
      case 8u: {
        if (integral_value < static_cast<double>(std::numeric_limits<int8_t>::min()) ||
            integral_value > static_cast<double>(std::numeric_limits<int8_t>::max())) {
          _SetErrorMessage(out_error_message, "Signed 8-bit descriptor value is out of range.");
          return false;
        }
        const int8_t encoded = static_cast<int8_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      case 16u: {
        if (integral_value < static_cast<double>(std::numeric_limits<int16_t>::min()) ||
            integral_value > static_cast<double>(std::numeric_limits<int16_t>::max())) {
          _SetErrorMessage(out_error_message, "Signed 16-bit descriptor value is out of range.");
          return false;
        }
        const int16_t encoded = static_cast<int16_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      case 32u: {
        if (integral_value < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
            integral_value > static_cast<double>(std::numeric_limits<int32_t>::max())) {
          _SetErrorMessage(out_error_message, "Signed 32-bit descriptor value is out of range.");
          return false;
        }
        const int32_t encoded = static_cast<int32_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      case 64u: {
        if (integral_value < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
            integral_value > static_cast<double>(std::numeric_limits<int64_t>::max())) {
          _SetErrorMessage(out_error_message, "Signed 64-bit descriptor value is out of range.");
          return false;
        }
        const int64_t encoded = static_cast<int64_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      default:
        _SetErrorMessage(out_error_message, "Signed integer descriptor codec only supports 8/16/32/64-bit targets.");
        return false;
    }
  }

  if (value_codec == "uint") {
    if (!_ValidateIntegerScalarValue(value, "unsigned integer", container->name, out_error_message)) {
      return false;
    }
    if (value < 0.0) {
      _SetErrorMessage(out_error_message, "Unsigned descriptor value cannot be negative.");
      return false;
    }
    const double integral_value = static_cast<double>(value);
    switch (scalar_bits) {
      case 8u: {
        if (integral_value > static_cast<double>(std::numeric_limits<uint8_t>::max())) {
          _SetErrorMessage(out_error_message, "Unsigned 8-bit descriptor value is out of range.");
          return false;
        }
        const uint8_t encoded = static_cast<uint8_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      case 16u: {
        if (integral_value > static_cast<double>(std::numeric_limits<uint16_t>::max())) {
          _SetErrorMessage(out_error_message, "Unsigned 16-bit descriptor value is out of range.");
          return false;
        }
        const uint16_t encoded = static_cast<uint16_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      case 32u: {
        if (integral_value > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
          _SetErrorMessage(out_error_message, "Unsigned 32-bit descriptor value is out of range.");
          return false;
        }
        const uint32_t encoded = static_cast<uint32_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      case 64u: {
        if (integral_value > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
          _SetErrorMessage(out_error_message, "Unsigned 64-bit descriptor value is out of range.");
          return false;
        }
        const uint64_t encoded = static_cast<uint64_t>(value);
        std::memcpy(destination, &encoded, sizeof(encoded));
        return true;
      }
      default:
        _SetErrorMessage(out_error_message, "Unsigned integer descriptor codec only supports 8/16/32/64-bit targets.");
        return false;
    }
  }

  _SetErrorMessage(out_error_message, "Unsupported descriptor codec '" + value_codec + "'.");
  return false;
}

bool _ReadCpuInterStageScalarAt(
  const algorithmManager::CpuPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  uint32_t index,
  float* out_value,
  std::string* out_error_message) {
  if (!inter_stage_buffer.valid) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer is invalid.");
    return false;
  }
  if (!out_value) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer read output pointer is null.");
    return false;
  }
  if (index >= inter_stage_buffer.scalar_slots.size() ||
      index >= inter_stage_buffer.scalar_slot_count) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer scalar read is out of range.");
    return false;
  }
  *out_value = inter_stage_buffer.scalar_slots[index];
  return true;
}

bool _WriteCpuInterStageScalarAt(
  algorithmManager::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  uint32_t index,
  float value,
  std::string* out_error_message) {
  if (!inter_stage_buffer) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer write target is null.");
    return false;
  }
  if (!inter_stage_buffer->valid) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer is invalid.");
    return false;
  }
  if (index >= inter_stage_buffer->scalar_slots.size() ||
      index >= inter_stage_buffer->scalar_slot_count) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer scalar write is out of range.");
    return false;
  }
  inter_stage_buffer->scalar_slots[index] = value;
  return true;
}

bool _ApplyImplicitStageBufferIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const algorithmManager::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::unordered_set<std::string>* written_target_names,
  std::string* out_error_message) {
  if (!target_container_set || !written_target_names) {
    _SetErrorMessage(out_error_message, "Pipeline implicit stage buffer ingress inputs are null.");
    return false;
  }
  const algorithm::AlgorithmRuntimeTransferStageLayout* stage_layout =
    _FindRuntimeTransferStageLayout(transfer_map, target_stage_name);
  if (!stage_layout) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer ingress layout is missing for stage '" + target_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_array_count != 0u) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer ingress does not support extra standard arrays for stage '" +
        target_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_variable_count == 0u) {
    return true;
  }

  for (uint32_t extra_index = 0u; extra_index < stage_layout->extra_variable_count; ++extra_index) {
    const std::string variable_name =
      target_container_set->standard_layout.MakeVariableName(stage_layout->shared_variable_count + extra_index);
    if (written_target_names->find(variable_name) != written_target_names->end()) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable is written more than once: " +
          target_stage_name + "." + variable_name);
      return false;
    }

    algorithm::AlgorithmContainer* target_variable =
      algorithm::FindAlgorithmContainer(target_container_set, variable_name);
    if (!target_variable) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable is missing: " +
          target_stage_name + "." + variable_name);
      return false;
    }
    if (target_variable->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister ||
        target_variable->bytes.size() < sizeof(float)) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable must be a scalar register: " +
          target_stage_name + "." + variable_name);
      return false;
    }

    float value = 0.0f;
    if (inter_stage_buffer) {
      if (!_ReadCpuInterStageScalarAt(
            *inter_stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            &value,
            out_error_message)) {
        return false;
      }
    } else {
      algorithm::AlgorithmContainer* stage_buffer =
        algorithm::FindAlgorithmContainer(target_container_set, transfer_map.pipeline_shared_stage_buffer_slot_name);
      if (!stage_buffer) {
        _SetErrorMessage(
          out_error_message,
          "Pipeline implicit stage buffer ingress cannot find shared stage buffer '" +
            transfer_map.pipeline_shared_stage_buffer_slot_name + "' for stage '" + target_stage_name + "'.");
        return false;
      }
      if (!_ReadArrayScalarAt(
            *stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            &value,
            out_error_message)) {
        return false;
      }
    }
    std::memcpy(target_variable->bytes.data(), &value, sizeof(float));
    written_target_names->insert(variable_name);
  }

  return true;
}

bool _ApplyImplicitStageBufferEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithmManager::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::string* out_error_message) {
  const algorithm::AlgorithmRuntimeTransferStageLayout* stage_layout =
    _FindRuntimeTransferStageLayout(transfer_map, source_stage_name);
  if (!stage_layout) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer egress layout is missing for stage '" + source_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_array_count != 0u) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer egress does not support extra standard arrays for stage '" +
        source_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_variable_count == 0u) {
    return true;
  }

  for (uint32_t extra_index = 0u; extra_index < stage_layout->extra_variable_count; ++extra_index) {
    const std::string variable_name =
      source_container_set.standard_layout.MakeVariableName(stage_layout->shared_variable_count + extra_index);
    const algorithm::AlgorithmContainer* source_variable =
      algorithm::FindAlgorithmContainer(source_container_set, variable_name);
    if (!source_variable) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer egress source variable is missing: " +
          source_stage_name + "." + variable_name);
      return false;
    }
    if (source_variable->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister ||
        source_variable->bytes.size() < sizeof(float)) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer egress source variable must be a scalar register: " +
          source_stage_name + "." + variable_name);
      return false;
    }

    float value = 0.0f;
    std::memcpy(&value, source_variable->bytes.data(), sizeof(float));
    if (inter_stage_buffer) {
      if (!_WriteCpuInterStageScalarAt(
            inter_stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            value,
            out_error_message)) {
        return false;
      }
    } else {
      algorithm::AlgorithmContainer* stage_buffer =
        algorithm::FindAlgorithmContainer(
          const_cast<algorithm::AlgorithmContainerSet*>(&source_container_set),
          transfer_map.pipeline_shared_stage_buffer_slot_name);
      if (!stage_buffer) {
        _SetErrorMessage(
          out_error_message,
          "Pipeline implicit stage buffer egress cannot find shared stage buffer '" +
            transfer_map.pipeline_shared_stage_buffer_slot_name + "' for stage '" + source_stage_name + "'.");
        return false;
      }
      if (!_WriteArrayScalarAt(
            stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            value,
            out_error_message)) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace

bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithmManager::AlgorithmRequestedResources* out_requested_resources,
  algorithmManager::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message) {
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }
  const PackageSchemaDecomposer decomposer(package_location);
  if (!decomposer.valid()) {
    _SetErrorMessage(out_error_message, decomposer.error_message());
    return false;
  }
  const algorithm::AlgorithmProfile profile{
    .algorithm_name = package_location.algorithm_name,
    .container_manifest_name = package_location.manifest_name,
  };
  if (!decomposer.GetRequestedResources(profile, out_requested_resources) ||
      !decomposer.GetRequestedDescriptorBindings(profile, out_requested_descriptor_bindings)) {
    _SetErrorMessage(out_error_message, "Failed to query requested bindings from package JSON.");
    return false;
  }
  _SetErrorMessage(out_error_message, {});
  return true;
}

bool _PipelineStageBridgeIngressImpl(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithmManager::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!out_target_container_set) {
    _SetErrorMessage(out_error_message, "Runtime transfer target container set is null.");
    return false;
  }

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> incoming_edges =
    transfer_map.FindIncomingEdges(target_stage_name);
  if (incoming_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + target_stage_name + "' has multiple predecessors.");
    return false;
  }

  std::unordered_set<std::string> written_target_names{};
  for (const algorithm::AlgorithmRuntimeTransferEdge* edge : incoming_edges) {
    if (!edge) {
      continue;
    }
    if (!_ApplyRuntimeTransferEdgeToTarget(
          transfer_map,
          *edge,
          stage_container_sets,
          out_target_container_set,
          &written_target_names,
          out_error_message)) {
      return false;
    }
  }
  if (inter_stage_buffer) {
    if (!_ApplyImplicitStageBufferIngress(
          transfer_map,
          target_stage_name,
          inter_stage_buffer,
          out_target_container_set,
          &written_target_names,
          out_error_message)) {
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return _PipelineStageBridgeIngressImpl(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    nullptr,
    out_target_container_set,
    out_error_message);
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithmManager::CpuPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return _PipelineStageBridgeIngressImpl(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    &inter_stage_buffer,
    out_target_container_set,
    out_error_message);
}

bool _PipelineStageBridgeEgressImpl(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithmManager::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!stage_container_sets) {
    _SetErrorMessage(out_error_message, "Runtime transfer stage container set map is null.");
    return false;
  }
  if (inter_stage_buffer) {
    if (!_ApplyImplicitStageBufferEgress(
          transfer_map,
          source_stage_name,
          source_container_set,
          inter_stage_buffer,
          out_error_message)) {
      return false;
    }
  }

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
    transfer_map.FindOutgoingEdges(source_stage_name);
  if (outgoing_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + source_stage_name + "' has multiple successors.");
    return false;
  }
  if (outgoing_edges.empty()) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::unordered_map<std::string, std::unordered_set<std::string>> written_target_names_by_stage{};
  for (const algorithm::AlgorithmRuntimeTransferEdge* edge : outgoing_edges) {
    if (!edge) {
      continue;
    }
    if (!_ApplyRuntimeTransferEdgeFromSource(
          transfer_map,
          *edge,
          source_container_set,
          stage_container_sets,
          &written_target_names_by_stage,
          out_error_message)) {
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return _PipelineStageBridgeEgressImpl(
    transfer_map,
    source_stage_name,
    source_container_set,
    nullptr,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithmManager::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return _PipelineStageBridgeEgressImpl(
    transfer_map,
    source_stage_name,
    source_container_set,
    inter_stage_buffer,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algorithmManager::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message) {
  (void)stage_container_sets;
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!out_debug_set) {
    _SetErrorMessage(out_error_message, "Pipeline bridge debug set output pointer is null.");
    return false;
  }

  out_debug_set->Clear();
  out_debug_set->pipeline_name = pipeline_name.empty() ? transfer_map.algorithm_name : pipeline_name;
  out_debug_set->stage_name = target_stage_name;

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> incoming_edges =
    transfer_map.FindIncomingEdges(target_stage_name);
  if (incoming_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + target_stage_name + "' has multiple predecessors.");
    return false;
  }

  if (!incoming_edges.empty() && incoming_edges.front()) {
    out_debug_set->previous_stage_name = incoming_edges.front()->source_stage_name;
    _CopyBridgeDebugBindings(incoming_edges.front(), &out_debug_set->ingress_bindings);
  }

  algorithm::CopyAlgorithmContainerSet(
    target_container_set,
    &out_debug_set->stage_input_container_set);
  out_debug_set->has_stage_input_container_set = true;
  out_debug_set->valid = true;

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithmManager::PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!in_out_debug_set) {
    _SetErrorMessage(out_error_message, "Pipeline bridge debug set output pointer is null.");
    return false;
  }

  if (!in_out_debug_set->valid) {
    in_out_debug_set->Clear();
  }
  if (in_out_debug_set->pipeline_name.empty()) {
    in_out_debug_set->pipeline_name = pipeline_name.empty() ? transfer_map.algorithm_name : pipeline_name;
  }
  in_out_debug_set->stage_name = source_stage_name;

  algorithm::CopyAlgorithmContainerSet(
    source_container_set,
    &in_out_debug_set->stage_output_container_set);
  in_out_debug_set->has_stage_output_container_set = true;

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
    transfer_map.FindOutgoingEdges(source_stage_name);
  if (outgoing_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + source_stage_name + "' has multiple successors.");
    return false;
  }

  in_out_debug_set->next_stage_name.clear();
  in_out_debug_set->egress_bindings.clear();
  in_out_debug_set->next_stage_input_container_set = {};
  in_out_debug_set->has_next_stage_input_container_set = false;

  if (!outgoing_edges.empty() && outgoing_edges.front()) {
    const algorithm::AlgorithmRuntimeTransferEdge* outgoing_edge = outgoing_edges.front();
    in_out_debug_set->next_stage_name = outgoing_edge->target_stage_name;
    _CopyBridgeDebugBindings(outgoing_edge, &in_out_debug_set->egress_bindings);

    const auto target_stage_it = stage_container_sets.find(outgoing_edge->target_stage_name);
    if (target_stage_it == stage_container_sets.end() || !target_stage_it->second) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target stage is unavailable for bridge debug capture: " +
          outgoing_edge->target_stage_name);
      return false;
    }

    algorithm::CopyAlgorithmContainerSet(
      *target_stage_it->second,
      &in_out_debug_set->next_stage_input_container_set);
    in_out_debug_set->has_next_stage_input_container_set = true;
  }

  in_out_debug_set->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DecomposeAlgorithmPackageFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  const std::vector<algorithmManager::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithmManager::AlgorithmDescriptorValue>& descriptor_values,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }
  const PackageSchemaDecomposer decomposer(package_location);
  if (!decomposer.valid()) {
    _SetErrorMessage(out_error_message, decomposer.error_message());
    return false;
  }
  const algorithm::AlgorithmProfile profile{
    .algorithm_name = package_location.algorithm_name,
    .container_manifest_name = package_location.manifest_name,
  };
  const bool ok = decomposer.Decompose(
    profile,
    resource_bindings,
    descriptor_values,
    container_set,
    out_error_message);
  return ok;
}

}  // namespace algorithm_support
