#include "algorithm_container_manifest.h"

#include "cJSON.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace algorithm {

namespace {

enum class ManifestSection {
  Variable,
  VariableArray,
};

struct ParsedItem {
  std::string name;
  std::string kind{"scalar"};
  std::string precision;
  std::vector<uint32_t> shape;
  uint32_t count{1};
  std::string count_from;
};

std::string _Trim(std::string value) {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(), value.end());
  return value;
}

std::string _ReadFileText(const std::string& path, std::string* out_error_message) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (out_error_message) {
      *out_error_message = "Failed to open JSON file: " + path;
    }
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetStringField(const cJSON* object, const char* key, const std::string& fallback = {}) {
  if (!object || !key) return fallback;
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) return fallback;
  return item->valuestring;
}

uint32_t _GetUintField(const cJSON* object, const char* key, uint32_t fallback = 0u) {
  if (!object || !key) return fallback;
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item)) return fallback;
  if (item->valuedouble < 0.0) return fallback;
  const double value = item->valuedouble;
  if (value > static_cast<double>(std::numeric_limits<uint32_t>::max())) return fallback;
  return static_cast<uint32_t>(value);
}

std::vector<uint32_t> _GetShapeField(const cJSON* object) {
  std::vector<uint32_t> shape;
  if (!object) return shape;

  const cJSON* shape_item = cJSON_GetObjectItemCaseSensitive(object, "shape");
  if (!shape_item || !cJSON_IsArray(shape_item)) {
    shape_item = cJSON_GetObjectItemCaseSensitive(object, "dim");
  }
  if (!shape_item || !cJSON_IsArray(shape_item)) {
    return shape;
  }

  const int count = cJSON_GetArraySize(shape_item);
  shape.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* dim = cJSON_GetArrayItem(shape_item, i);
    if (!dim || !cJSON_IsNumber(dim) || dim->valuedouble < 0.0) continue;
    if (dim->valuedouble > static_cast<double>(std::numeric_limits<uint32_t>::max())) continue;
    shape.push_back(static_cast<uint32_t>(dim->valuedouble));
  }
  return shape;
}

uint32_t _ElementCountFromShape(const std::vector<uint32_t>& shape) {
  if (shape.empty()) return 1u;
  uint64_t total = 1u;
  for (uint32_t dim : shape) {
    total *= std::max<uint32_t>(1u, dim);
    if (total > std::numeric_limits<uint32_t>::max()) {
      return std::numeric_limits<uint32_t>::max();
    }
  }
  return static_cast<uint32_t>(total);
}

uint32_t _PrecisionToStrideBytes(const std::string& precision, const std::vector<uint32_t>& shape) {
  const std::string normalized = _Trim(precision);
  uint32_t scalar_bytes = 4u;
  if (normalized == "fp16" || normalized == "f16" || normalized == "half") {
    scalar_bytes = 2u;
  } else if (normalized == "fp64" || normalized == "f64" || normalized == "double") {
    scalar_bytes = 8u;
  } else if (normalized == "i8" || normalized == "u8") {
    scalar_bytes = 1u;
  } else if (normalized == "i16" || normalized == "u16") {
    scalar_bytes = 2u;
  } else if (normalized == "i32" || normalized == "u32" || normalized == "fp32" || normalized == "float") {
    scalar_bytes = 4u;
  }

  uint64_t stride = scalar_bytes;
  for (uint32_t dim : shape) {
    stride *= std::max<uint32_t>(1u, dim);
    if (stride > std::numeric_limits<uint32_t>::max()) {
      return std::numeric_limits<uint32_t>::max();
    }
  }
  return static_cast<uint32_t>(stride);
}

std::vector<ParsedItem> _ParseItemObject(
  const cJSON* object,
  const std::string& default_name,
  const std::string& default_precision,
  ManifestSection section) {
  std::vector<ParsedItem> items;
  if (!object) return items;

  if (cJSON_IsArray(object)) {
    const int count = cJSON_GetArraySize(object);
    items.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
    for (int i = 0; i < count; ++i) {
      const cJSON* child = cJSON_GetArrayItem(object, i);
      if (!child) continue;
      ParsedItem item{};
      item.name = default_name + "_" + std::to_string(i);
      item.precision = default_precision;
      item.shape = {};
      item.count = 1u;
      if (const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(child, "name"); name_item && cJSON_IsString(name_item) && name_item->valuestring) {
        item.name = name_item->valuestring;
      }
      if (const cJSON* precision_item = cJSON_GetObjectItemCaseSensitive(child, "precision"); precision_item && cJSON_IsString(precision_item) && precision_item->valuestring) {
        item.precision = precision_item->valuestring;
      }
      if (const cJSON* count_from_item = cJSON_GetObjectItemCaseSensitive(child, "count_from"); count_from_item && cJSON_IsString(count_from_item) && count_from_item->valuestring) {
        item.count_from = count_from_item->valuestring;
      }
      item.shape = _GetShapeField(child);
      item.count = _GetUintField(child, "count", _ElementCountFromShape(item.shape));
      if (section == ManifestSection::VariableArray) {
        item.kind = "array";
      }
      items.push_back(std::move(item));
    }
    return items;
  }

  ParsedItem item{};
  item.name = default_name;
  item.precision = default_precision;
  item.shape = _GetShapeField(object);
  item.count = _GetUintField(object, "count", _ElementCountFromShape(item.shape));
  if (const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(object, "name"); name_item && cJSON_IsString(name_item) && name_item->valuestring) {
    item.name = name_item->valuestring;
  }
  if (const cJSON* kind_item = cJSON_GetObjectItemCaseSensitive(object, "kind"); kind_item && cJSON_IsString(kind_item) && kind_item->valuestring) {
    item.kind = kind_item->valuestring;
  } else {
    item.kind = section == ManifestSection::VariableArray ? "array" : "scalar";
  }
  if (const cJSON* precision_item = cJSON_GetObjectItemCaseSensitive(object, "precision"); precision_item && cJSON_IsString(precision_item) && precision_item->valuestring) {
    item.precision = precision_item->valuestring;
  }
  if (const cJSON* count_from_item = cJSON_GetObjectItemCaseSensitive(object, "count_from"); count_from_item && cJSON_IsString(count_from_item) && count_from_item->valuestring) {
    item.count_from = count_from_item->valuestring;
  }
  if (item.shape.empty() && section == ManifestSection::VariableArray) {
    item.count = std::max<uint32_t>(1u, item.count);
  }
  items.push_back(std::move(item));
  return items;
}

bool _ParseManifestSection(
  const cJSON* section_object,
  const std::string& default_precision,
  ManifestSection section_kind,
  std::vector<AlgorithmContainerManifestItem>* out_items) {
  if (!out_items) return false;
  if (!section_object || !cJSON_IsObject(section_object)) return true;

  for (const cJSON* child = section_object->child; child; child = child->next) {
    if (!child->string || !*child->string) continue;
    const std::string name = child->string;
    std::vector<ParsedItem> parsed = _ParseItemObject(child, name, default_precision, section_kind);
    for (const ParsedItem& item : parsed) {
      AlgorithmContainerManifestItem out{};
      out.name = item.name;
      out.kind = item.kind;
      out.precision = item.precision.empty() ? default_precision : item.precision;
      out.shape = item.shape;
      out.count = item.count;
      out.count_specified = item.count != 1u || !item.shape.empty() || !item.count_from.empty();
      out.count_from = item.count_from;
      out_items->push_back(std::move(out));
    }
  }
  return true;
}

bool _LoadManifestFromJsonRoot(
  const cJSON* root,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message) {
  if (!out_manifest) return false;
  if (!root || !cJSON_IsObject(root)) {
    if (out_error_message) {
      *out_error_message = "JSON root must be an object.";
    }
    return false;
  }

  const cJSON* global_cfg = cJSON_GetObjectItemCaseSensitive(root, "globalCfg");
  if (!global_cfg || !cJSON_IsObject(global_cfg)) {
    if (out_error_message) {
      *out_error_message = "Missing required top-level object: globalCfg.";
    }
    return false;
  }

  const std::string algorithm_name = _GetStringField(root, "algorithm_name", _GetStringField(root, "name", ""));
  const std::string solve_precision = _GetStringField(global_cfg, "solvePrecision", _GetStringField(global_cfg, "解算精度", "fp32"));
  const std::string default_precision = _GetStringField(global_cfg, "defaultPrecision", solve_precision);

  out_manifest->algorithm_name = algorithm_name;
  out_manifest->solve_precision = solve_precision;
  out_manifest->variables.clear();
  out_manifest->variable_arrays.clear();

  const cJSON* variables = cJSON_GetObjectItemCaseSensitive(root, "variable");
  const cJSON* variable_arrays = cJSON_GetObjectItemCaseSensitive(root, "variableArray");

  if (variables && !cJSON_IsObject(variables)) {
    if (out_error_message) {
      *out_error_message = "Top-level 'variable' must be an object.";
    }
    return false;
  }
  if (variable_arrays && !cJSON_IsObject(variable_arrays)) {
    if (out_error_message) {
      *out_error_message = "Top-level 'variableArray' must be an object.";
    }
    return false;
  }

  if (!variables && !variable_arrays) {
    if (out_error_message) {
      *out_error_message = "Manifest must contain at least one of 'variable' or 'variableArray'.";
    }
    return false;
  }

  if (variables) {
    if (!_ParseManifestSection(variables, default_precision, ManifestSection::Variable, &out_manifest->variables)) {
      return false;
    }
  }
  if (variable_arrays) {
    if (!_ParseManifestSection(variable_arrays, default_precision, ManifestSection::VariableArray, &out_manifest->variable_arrays)) {
      return false;
    }
  }

  return true;
}

uint32_t _PrecisionToStride(const std::string& precision, const std::vector<uint32_t>& shape) {
  return _PrecisionToStrideBytes(precision, shape);
}

void _AppendRequirement(
  const AlgorithmContainerManifestItem& item,
  AlgorithmDataContract* contract,
  bool as_array) {
  if (!contract) return;
  const uint32_t stride = _PrecisionToStride(item.precision, item.shape);
  const uint32_t count = as_array ? std::max<uint32_t>(1u, item.count) : 1u;
  AlgorithmBufferRequirement requirement{item.name, count, stride};
  AlgorithmDataFormat format{item.name, count, stride};
  if (as_array) {
    contract->arrays_to_allocate.push_back(requirement);
  } else {
    contract->temporary_registers_to_allocate.push_back(requirement);
  }
  contract->filled_data_formats.push_back(format);
  contract->algorithm_required_formats.push_back(format);
}

}  // namespace

bool LoadAlgorithmContainerManifestFromJsonText(
  const std::string& json_text,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message) {
  if (!out_manifest) return false;
  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse JSON text.";
    }
    return false;
  }
  const bool ok = _LoadManifestFromJsonRoot(root, out_manifest, out_error_message);
  cJSON_Delete(root);
  return ok;
}

bool LoadAlgorithmContainerManifestFromJsonFile(
  const std::string& path,
  AlgorithmContainerManifest* out_manifest,
  std::string* out_error_message) {
  const std::string json_text = _ReadFileText(path, out_error_message);
  if (json_text.empty()) {
    return false;
  }
  return LoadAlgorithmContainerManifestFromJsonText(json_text, out_manifest, out_error_message);
}

AlgorithmContainerDescriptor BuildAlgorithmContainerDescriptor(const AlgorithmContainerManifest& manifest) {
  AlgorithmContainerDescriptor descriptor{};
  descriptor.algorithm_name = manifest.algorithm_name;
  descriptor.cpu_available = true;
  descriptor.gpu_available = false;
  descriptor.motion_radius = 0.0f;

  for (const auto& item : manifest.variables) {
    _AppendRequirement(item, &descriptor.data_contract, item.kind == "array");
  }
  for (const auto& item : manifest.variable_arrays) {
    _AppendRequirement(item, &descriptor.data_contract, true);
  }

  return descriptor;
}

bool LoadAlgorithmContainerDescriptorFromJsonText(
  const std::string& json_text,
  AlgorithmContainerDescriptor* out_descriptor,
  std::string* out_error_message) {
  if (!out_descriptor) return false;
  AlgorithmContainerManifest manifest{};
  if (!LoadAlgorithmContainerManifestFromJsonText(json_text, &manifest, out_error_message)) {
    return false;
  }
  *out_descriptor = BuildAlgorithmContainerDescriptor(manifest);
  return true;
}

bool LoadAlgorithmContainerDescriptorFromJsonFile(
  const std::string& path,
  AlgorithmContainerDescriptor* out_descriptor,
  std::string* out_error_message) {
  if (!out_descriptor) return false;
  const std::string json_text = _ReadFileText(path, out_error_message);
  if (json_text.empty()) {
    return false;
  }
  return LoadAlgorithmContainerDescriptorFromJsonText(json_text, out_descriptor, out_error_message);
}

}  // namespace algorithm
