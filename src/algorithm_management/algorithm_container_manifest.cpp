#include "algorithm_container_manifest.h"

#include "runtime_systems/memory_manager.h"

#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace algorithm {

namespace {

namespace fs = std::filesystem;

#ifndef ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT
#define ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT "src/capabilities/algorithm_library"
#endif

enum class ManifestSection {
  Variable,
  VariableArray,
};

enum class ManifestLookupResult {
  Found,
  NotFound,
  Error,
};

struct ParsedItem {
  std::string name;
  std::string kind{"scalar"};
  std::string precision;
  std::vector<uint32_t> shape;
  uint32_t count{1};
  std::string count_from;
};

struct ManifestTemplateCache {
  std::mutex mutex;
  std::unordered_map<std::string, AlgorithmContainerSet> templates_by_manifest_path;
  std::unordered_map<std::string, AlgorithmReflector> reflectors_by_manifest_path;
};

AlgorithmContainerManifestItem _MakeManifestItem(
  const std::string& name,
  AlgorithmContainerStorageKind storage_kind,
  const std::string& default_precision) {
  AlgorithmContainerManifestItem item{};
  item.name = name;
  item.kind = storage_kind == AlgorithmContainerStorageKind::Array ? "array" : "scalar";
  item.precision = default_precision;
  item.count = 1u;
  item.count_specified = true;
  return item;
}

void _AppendStandardLayout(
  const AlgorithmStandardContainerLayout& layout,
  const std::string& default_precision,
  std::vector<AlgorithmContainerManifestItem>* out_variables,
  std::vector<AlgorithmContainerManifestItem>* out_variable_arrays) {
  if (!layout.enabled()) {
    return;
  }

  if (out_variables) {
    out_variables->reserve(out_variables->size() + static_cast<size_t>(layout.variable_count));
    for (uint32_t i = 0; i < layout.variable_count; ++i) {
      out_variables->push_back(_MakeManifestItem(
        layout.variable_prefix + std::to_string(i + 1u),
        AlgorithmContainerStorageKind::TemporaryRegister,
        default_precision));
    }
  }

  if (out_variable_arrays) {
    out_variable_arrays->reserve(out_variable_arrays->size() + static_cast<size_t>(layout.array_count));
    for (uint32_t i = 0; i < layout.array_count; ++i) {
      out_variable_arrays->push_back(_MakeManifestItem(
        layout.array_prefix + std::to_string(i + 1u),
        AlgorithmContainerStorageKind::Array,
        default_precision));
    }
  }
}

std::string _Trim(std::string value) {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(), value.end());
  return value;
}

std::string _ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string _NormalizeManifestLookupKey(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return normalized;
}

std::string _ReadFileText(const std::string& path, std::string* out_error_message) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (out_error_message) {
      *out_error_message = "Failed to open manifest JSON file: " + path;
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

std::vector<std::string> _GetStringList(const cJSON* item) {
  std::vector<std::string> values;
  if (!item) return values;

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
    if (!entry || !cJSON_IsString(entry) || !entry->valuestring) continue;
    values.emplace_back(entry->valuestring);
  }
  return values;
}

std::vector<std::string> _GetStringListField(
  const cJSON* object,
  std::initializer_list<const char*> keys) {
  if (!object || !cJSON_IsObject(object)) {
    return {};
  }

  for (const char* key : keys) {
    if (!key) continue;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    std::vector<std::string> values = _GetStringList(item);
    if (!values.empty()) {
      return values;
    }
  }

  return {};
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
      if (!child || !cJSON_IsObject(child)) continue;

      ParsedItem item{};
      item.name = default_name + "_" + std::to_string(i);
      item.precision = default_precision;
      item.count = 1u;

      if (const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(child, "name"); name_item && cJSON_IsString(name_item) && name_item->valuestring) {
        item.name = name_item->valuestring;
      }
      if (const cJSON* kind_item = cJSON_GetObjectItemCaseSensitive(child, "kind"); kind_item && cJSON_IsString(kind_item) && kind_item->valuestring) {
        item.kind = kind_item->valuestring;
      } else if (section == ManifestSection::VariableArray) {
        item.kind = "array";
      }
      if (const cJSON* precision_item = cJSON_GetObjectItemCaseSensitive(child, "precision"); precision_item && cJSON_IsString(precision_item) && precision_item->valuestring) {
        item.precision = precision_item->valuestring;
      }
      if (const cJSON* count_from_item = cJSON_GetObjectItemCaseSensitive(child, "count_from"); count_from_item && cJSON_IsString(count_from_item) && count_from_item->valuestring) {
        item.count_from = count_from_item->valuestring;
      }

      item.shape = _GetShapeField(child);
      item.count = _GetUintField(child, "count", _ElementCountFromShape(item.shape));
      items.push_back(std::move(item));
    }
    return items;
  }

  if (!cJSON_IsObject(object)) {
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

bool _ParseReflectorManifestItem(
  const cJSON* item,
  const std::string& default_reflection_object_name,
  AlgorithmReflectorManifestItem* out_item,
  std::string* out_error_message) {
  if (!out_item) return false;

  AlgorithmReflectorManifestItem parsed{};
  if (cJSON_IsString(item) && item->valuestring) {
    parsed.container_names.push_back(item->valuestring);
    parsed.reflection_object_name = default_reflection_object_name;
  } else if (cJSON_IsArray(item)) {
    parsed.container_names = _GetStringList(item);
    parsed.reflection_object_name = default_reflection_object_name;
  } else if (cJSON_IsObject(item)) {
    parsed.reflection_object_name = _GetStringField(
      item,
      "name",
      _GetStringField(
        item,
        "reflection_object",
        _GetStringField(
          item,
          "target",
          _GetStringField(item, "to", default_reflection_object_name))));
    parsed.filter_name = _GetStringField(
      item,
      "filter",
      _GetStringField(item, "reflection_filter", ""));
    parsed.container_names = _GetStringListField(
      item,
      {"containers", "container_names", "container", "from", "source", "sources", "variable", "variables"});
  } else {
    if (out_error_message) {
      *out_error_message = "Each reflector entry must be a string, array, or object.";
    }
    return false;
  }

  parsed.reflection_object_name = _Trim(parsed.reflection_object_name);
  parsed.filter_name = _Trim(parsed.filter_name);
  for (std::string& container_name : parsed.container_names) {
    container_name = _Trim(std::move(container_name));
  }
  parsed.container_names.erase(
    std::remove_if(
      parsed.container_names.begin(),
      parsed.container_names.end(),
      [](const std::string& value) { return value.empty(); }),
    parsed.container_names.end());

  if (parsed.reflection_object_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Reflector entry is missing a reflection object name.";
    }
    return false;
  }
  if (parsed.container_names.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Reflector entry '" + parsed.reflection_object_name + "' is missing source containers.";
    }
    return false;
  }

  *out_item = std::move(parsed);
  return true;
}

bool _ParseReflectorSection(
  const cJSON* section_object,
  std::vector<AlgorithmReflectorManifestItem>* out_items,
  std::string* out_error_message) {
  if (!out_items) return false;
  if (!section_object) return true;

  if (cJSON_IsArray(section_object)) {
    const int count = cJSON_GetArraySize(section_object);
    out_items->reserve(out_items->size() + (count > 0 ? static_cast<size_t>(count) : 0u));
    for (int i = 0; i < count; ++i) {
      const cJSON* child = cJSON_GetArrayItem(section_object, i);
      if (!child) continue;
      AlgorithmReflectorManifestItem item{};
      if (!_ParseReflectorManifestItem(child, "", &item, out_error_message)) {
        return false;
      }
      out_items->push_back(std::move(item));
    }
    return true;
  }

  if (!cJSON_IsObject(section_object)) {
    if (out_error_message) {
      *out_error_message = "Top-level 'reflector' must be an object or array.";
    }
    return false;
  }

  for (const cJSON* child = section_object->child; child; child = child->next) {
    if (!child->string || !*child->string) continue;

    AlgorithmReflectorManifestItem item{};
    if (!_ParseReflectorManifestItem(child, child->string, &item, out_error_message)) {
      return false;
    }
    out_items->push_back(std::move(item));
  }
  return true;
}

bool _ParseStandardLayoutSection(
  const cJSON* section_object,
  AlgorithmStandardContainerLayout* out_layout) {
  if (!out_layout) return false;
  *out_layout = {};
  if (!section_object || !cJSON_IsObject(section_object)) {
    return true;
  }

  out_layout->layout_name = _GetStringField(section_object, "name", _GetStringField(section_object, "layout_name", ""));
  out_layout->layout_kind = _GetStringField(section_object, "kind", _GetStringField(section_object, "layout_kind", ""));
  out_layout->variable_count = _GetUintField(section_object, "variable_count", _GetUintField(section_object, "variables", 0u));
  out_layout->array_count = _GetUintField(section_object, "array_count", _GetUintField(section_object, "arrays", 0u));
  out_layout->variable_prefix = _GetStringField(section_object, "variable_prefix", _GetStringField(section_object, "variable_slot_prefix", "v"));
  out_layout->array_prefix = _GetStringField(section_object, "array_prefix", _GetStringField(section_object, "array_slot_prefix", "a"));

  if (out_layout->variable_prefix.empty()) {
    out_layout->variable_prefix = "v";
  }
  if (out_layout->array_prefix.empty()) {
    out_layout->array_prefix = "a";
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
      *out_error_message = "Manifest JSON root must be an object.";
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
  const std::string solve_precision = _GetStringField(global_cfg, "solvePrecision", _GetStringField(global_cfg, "瑙ｇ畻绮惧害", "fp32"));
  const std::string default_precision = _GetStringField(global_cfg, "defaultPrecision", solve_precision);

  out_manifest->algorithm_name = algorithm_name;
  out_manifest->solve_precision = solve_precision;
  out_manifest->standard_layout = {};
  out_manifest->variables.clear();
  out_manifest->variable_arrays.clear();
  out_manifest->reflectors.clear();

  const cJSON* standard_layout = cJSON_GetObjectItemCaseSensitive(root, "standardLayout");
  if (!standard_layout) {
    standard_layout = cJSON_GetObjectItemCaseSensitive(root, "standard_layout");
  }
  if (!standard_layout) {
    standard_layout = cJSON_GetObjectItemCaseSensitive(root, "standardContainer");
  }
  if (!standard_layout) {
    standard_layout = cJSON_GetObjectItemCaseSensitive(root, "standard_container");
  }
  if (!_ParseStandardLayoutSection(standard_layout, &out_manifest->standard_layout)) {
    if (out_error_message) {
      *out_error_message = "Failed to parse standard container layout.";
    }
    return false;
  }

  const cJSON* variables = cJSON_GetObjectItemCaseSensitive(root, "variable");
  const cJSON* variable_arrays = cJSON_GetObjectItemCaseSensitive(root, "variableArray");
  const cJSON* reflectors = cJSON_GetObjectItemCaseSensitive(root, "reflector");
  if (!reflectors) {
    reflectors = cJSON_GetObjectItemCaseSensitive(root, "reflectors");
  }
  if (!reflectors) {
    reflectors = cJSON_GetObjectItemCaseSensitive(root, "reflection");
  }

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
  }

  if (variables && !_ParseManifestSection(variables, default_precision, ManifestSection::Variable, &out_manifest->variables)) {
    return false;
  }
  if (variable_arrays && !_ParseManifestSection(variable_arrays, default_precision, ManifestSection::VariableArray, &out_manifest->variable_arrays)) {
    return false;
  }
  if (out_manifest->standard_layout.enabled()) {
    _AppendStandardLayout(
      out_manifest->standard_layout,
      default_precision,
      &out_manifest->variables,
      &out_manifest->variable_arrays);
  }
  if (out_manifest->variables.empty() && out_manifest->variable_arrays.empty()) {
    if (out_error_message) {
      *out_error_message = "Manifest must contain at least one of 'variable', 'variableArray', or 'standardLayout'.";
    }
    return false;
  }
  if (reflectors && !_ParseReflectorSection(reflectors, &out_manifest->reflectors, out_error_message)) {
    return false;
  }

  return true;
}

bool _TryResolveStorageKind(
  const AlgorithmContainerManifestItem& item,
  AlgorithmContainerStorageKind* out_storage_kind,
  std::string* out_error_message) {
  if (!out_storage_kind) return false;

  const std::string kind = _NormalizeManifestLookupKey(item.kind);
  if (kind.empty() || kind == "scalar" || kind == "register" || kind == "temporaryregister" || kind == "tempregister") {
    *out_storage_kind = AlgorithmContainerStorageKind::TemporaryRegister;
    return true;
  }
  if (kind == "array" || kind == "variablearray") {
    *out_storage_kind = AlgorithmContainerStorageKind::Array;
    return true;
  }
  if (kind == "cache" || kind == "temporarycache" || kind == "tempcache") {
    *out_storage_kind = AlgorithmContainerStorageKind::TemporaryCache;
    return true;
  }

  if (out_error_message) {
    *out_error_message = "Unsupported manifest container kind '" + item.kind + "' for '" + item.name + "'.";
  }
  return false;
}

bool _AppendRuntimeContainer(
  std::string_view algorithm_name,
  const AlgorithmContainerManifestItem& item,
  AlgorithmContainerStorageKind storage_kind,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) return false;
  (void)runtime_systems::MemoryManager::Instance();

  if (item.name.empty()) {
    if (out_error_message) {
      *out_error_message = "Encountered a manifest container entry with an empty name.";
    }
    return false;
  }

  const uint32_t element_count = storage_kind == AlgorithmContainerStorageKind::Array
    ? std::max<uint32_t>(1u, item.count)
    : 1u;
  const uint32_t element_stride = _PrecisionToStrideBytes(item.precision, item.shape);

  if (element_stride == 0u) {
    if (out_error_message) {
      *out_error_message = "Container '" + item.name + "' resolved to an element stride of 0.";
    }
    return false;
  }
  if (FindAlgorithmContainer(*out_container_set, item.name) != nullptr) {
    if (out_error_message) {
      *out_error_message = "Duplicate container name '" + item.name + "' while creating runtime containers for '" +
        std::string(algorithm_name) + "'.";
    }
    return false;
  }

  const uint64_t total_bytes_u64 =
    static_cast<uint64_t>(element_count) * static_cast<uint64_t>(element_stride);
  if (total_bytes_u64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    if (out_error_message) {
      *out_error_message = "Container '" + item.name + "' exceeds addressable size.";
    }
    return false;
  }

  AlgorithmContainer container{};
  container.name = item.name;
  container.storage_kind = storage_kind;
  container.element_count = element_count;
  container.element_stride = element_stride;
  container.bytes.resize(static_cast<size_t>(total_bytes_u64));

  switch (storage_kind) {
    case AlgorithmContainerStorageKind::Array:
      out_container_set->arrays.push_back(std::move(container));
      break;
    case AlgorithmContainerStorageKind::TemporaryRegister:
      out_container_set->temporary_registers.push_back(std::move(container));
      break;
    case AlgorithmContainerStorageKind::TemporaryCache:
      out_container_set->temporary_caches.push_back(std::move(container));
      break;
  }
  return true;
}

void _ClearContainerBytes(AlgorithmContainer* container) {
  if (!container) return;
  std::fill(container->bytes.begin(), container->bytes.end(), std::byte{0});
}

void _ClearContainerSetBytes(AlgorithmContainerSet* container_set) {
  if (!container_set) return;
  for (AlgorithmContainer& container : container_set->arrays) {
    _ClearContainerBytes(&container);
  }
  for (AlgorithmContainer& container : container_set->temporary_registers) {
    _ClearContainerBytes(&container);
  }
  for (AlgorithmContainer& container : container_set->temporary_caches) {
    _ClearContainerBytes(&container);
  }
}

AlgorithmContainerSet _CloneClearedContainerSet(const AlgorithmContainerSet& template_container_set) {
  AlgorithmContainerSet clone = template_container_set;
  _ClearContainerSetBytes(&clone);
  return clone;
}

const std::string& _ManifestSearchRoot() {
  static const std::string root = ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT;
  return root;
}

std::string _StableManifestCacheKey(const fs::path& manifest_path) {
  std::error_code ec;
  fs::path weakly_canonical_path = fs::weakly_canonical(manifest_path, ec);
  if (!ec) {
    return weakly_canonical_path.generic_string();
  }
  return fs::absolute(manifest_path, ec).generic_string();
}

bool _TryUseManifestPathCandidate(const fs::path& candidate, fs::path* out_manifest_path) {
  if (!out_manifest_path) return false;

  std::error_code ec;
  if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
    *out_manifest_path = candidate;
    return true;
  }

  if (!candidate.has_extension()) {
    fs::path with_json_extension = candidate;
    with_json_extension += ".json";
    if (fs::exists(with_json_extension, ec) && fs::is_regular_file(with_json_extension, ec)) {
      *out_manifest_path = with_json_extension;
      return true;
    }
  }

  return false;
}

bool _ManifestNameMatches(const fs::path& candidate, const std::string& manifest_name) {
  const std::string wanted = _NormalizeManifestLookupKey(manifest_name);
  if (wanted.empty()) return false;

  const std::string filename = candidate.filename().string();
  const std::string stem = candidate.stem().string();
  return _NormalizeManifestLookupKey(filename) == wanted ||
    _NormalizeManifestLookupKey(stem) == wanted;
}

ManifestLookupResult _ResolveManifestPathByName(
  const std::string& manifest_name,
  fs::path* out_manifest_path,
  std::string* out_error_message) {
  if (!out_manifest_path) return ManifestLookupResult::Error;

  const std::string trimmed_name = _Trim(manifest_name);
  if (trimmed_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Manifest name must not be empty.";
    }
    return ManifestLookupResult::Error;
  }

  const fs::path search_root = fs::path(_ManifestSearchRoot());
  std::error_code ec;
  if (!fs::exists(search_root, ec) || !fs::is_directory(search_root, ec)) {
    if (out_error_message) {
      *out_error_message = "Manifest search root does not exist: " + search_root.generic_string();
    }
    return ManifestLookupResult::Error;
  }

  const fs::path requested_path = fs::path(trimmed_name);
  if (_TryUseManifestPathCandidate(requested_path, out_manifest_path)) {
    return ManifestLookupResult::Found;
  }
  const fs::path folder_candidate = search_root / trimmed_name / (trimmed_name + ".json");
  if (_TryUseManifestPathCandidate(folder_candidate, out_manifest_path)) {
    return ManifestLookupResult::Found;
  }
  if (_TryUseManifestPathCandidate(search_root / requested_path, out_manifest_path)) {
    return ManifestLookupResult::Found;
  }

  std::vector<fs::path> matches;
  for (fs::recursive_directory_iterator iter(search_root, ec), end; !ec && iter != end; iter.increment(ec)) {
    if (ec) break;
    if (!iter->is_regular_file()) continue;
    const fs::path candidate = iter->path();
    if (_ToLower(candidate.extension().string()) != ".json") continue;
    if (_ManifestNameMatches(candidate, trimmed_name)) {
      matches.push_back(candidate);
    }
  }

  if (matches.size() == 1u) {
    *out_manifest_path = matches.front();
    return ManifestLookupResult::Found;
  }

  if (out_error_message) {
    if (matches.empty()) {
      *out_error_message =
        "No manifest named '" + trimmed_name + "' was found under " + search_root.generic_string() + ".";
    } else {
      std::ostringstream message;
      message << "Manifest name '" << trimmed_name << "' is ambiguous under "
              << search_root.generic_string() << ": ";
      for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0u) {
          message << ", ";
        }
        message << matches[i].generic_string();
      }
      *out_error_message = message.str();
    }
  }
  return matches.empty() ? ManifestLookupResult::NotFound : ManifestLookupResult::Error;
}

ManifestTemplateCache& _GetManifestTemplateCache() {
  static ManifestTemplateCache cache;
  return cache;
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
      *out_error_message = "Failed to parse manifest JSON text.";
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

bool CreateAlgorithmContainersFromManifest(
  const AlgorithmContainerManifest& manifest,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) {
    if (out_error_message) {
      *out_error_message = "AlgorithmContainerSet output pointer is null.";
    }
    return false;
  }

  AlgorithmContainerSet container_set{};
  container_set.algorithm_name = manifest.algorithm_name;
  container_set.standard_layout = manifest.standard_layout;

  for (const AlgorithmContainerManifestItem& item : manifest.variables) {
    AlgorithmContainerStorageKind storage_kind = AlgorithmContainerStorageKind::TemporaryRegister;
    if (!_TryResolveStorageKind(item, &storage_kind, out_error_message)) {
      return false;
    }
    if (!_AppendRuntimeContainer(manifest.algorithm_name, item, storage_kind, &container_set, out_error_message)) {
      return false;
    }
  }

  for (const AlgorithmContainerManifestItem& item : manifest.variable_arrays) {
    AlgorithmContainerStorageKind storage_kind = AlgorithmContainerStorageKind::Array;
    if (!_TryResolveStorageKind(item, &storage_kind, out_error_message)) {
      return false;
    }
    if (!_AppendRuntimeContainer(manifest.algorithm_name, item, storage_kind, &container_set, out_error_message)) {
      return false;
    }
  }

  *out_container_set = std::move(container_set);
  return true;
}

bool CreateAlgorithmReflectorFromManifest(
  const AlgorithmContainerManifest& manifest,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  AlgorithmReflector reflector{};
  reflector.algorithm_name = manifest.algorithm_name;

  std::unordered_set<std::string> declared_container_names;
  declared_container_names.reserve(manifest.variables.size() + manifest.variable_arrays.size());
  for (const AlgorithmContainerManifestItem& item : manifest.variables) {
    declared_container_names.insert(item.name);
  }
  for (const AlgorithmContainerManifestItem& item : manifest.variable_arrays) {
    declared_container_names.insert(item.name);
  }

  for (const AlgorithmReflectorManifestItem& item : manifest.reflectors) {
    if (item.reflection_object_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Reflector entry is missing a reflection object name.";
      }
      return false;
    }
    if (item.container_names.empty()) {
      if (out_error_message) {
        *out_error_message =
          "Reflector entry '" + item.reflection_object_name + "' is missing source containers.";
      }
      return false;
    }
    if (reflector.container_bindings_by_reflection_object_name.contains(item.reflection_object_name)) {
      if (out_error_message) {
        *out_error_message =
          "Duplicate reflection object name '" + item.reflection_object_name + "' in reflector manifest.";
      }
      return false;
    }

    for (const std::string& container_name : item.container_names) {
      if (!declared_container_names.contains(container_name)) {
        if (out_error_message) {
          *out_error_message =
            "Reflector entry '" + item.reflection_object_name +
            "' references unknown container '" + container_name + "'.";
        }
        return false;
      }
    }

    AlgorithmReflectionBinding binding{};
    binding.container_names = item.container_names;
    binding.reflection_object_name = item.reflection_object_name;
    binding.filter_name = item.filter_name;

    reflector.container_bindings_by_reflection_object_name.emplace(
      binding.reflection_object_name,
      binding);
    for (const std::string& container_name : binding.container_names) {
      reflector.reflection_objects_by_container_name[container_name].push_back(binding);
    }
  }

  *out_reflector = std::move(reflector);
  return true;
}

bool CreateAlgorithmContainersFromManifestFile(
  const std::string& path,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) {
    if (out_error_message) {
      *out_error_message = "AlgorithmContainerSet output pointer is null.";
    }
    return false;
  }

  const fs::path manifest_path(path);
  const std::string cache_key = _StableManifestCacheKey(manifest_path);
  ManifestTemplateCache& cache = _GetManifestTemplateCache();

  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    const auto cached = cache.templates_by_manifest_path.find(cache_key);
    if (cached != cache.templates_by_manifest_path.end()) {
      *out_container_set = _CloneClearedContainerSet(cached->second);
      return true;
    }
  }

  AlgorithmContainerManifest manifest{};
  if (!LoadAlgorithmContainerManifestFromJsonFile(path, &manifest, out_error_message)) {
    return false;
  }

  AlgorithmContainerSet template_container_set{};
  if (!CreateAlgorithmContainersFromManifest(manifest, &template_container_set, out_error_message)) {
    return false;
  }
  _ClearContainerSetBytes(&template_container_set);

  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto [it, inserted] = cache.templates_by_manifest_path.emplace(cache_key, std::move(template_container_set));
    (void)inserted;
    *out_container_set = _CloneClearedContainerSet(it->second);
  }

  return true;
}

bool CreateAlgorithmReflectorFromManifestFile(
  const std::string& path,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  const fs::path manifest_path(path);
  const std::string cache_key = _StableManifestCacheKey(manifest_path);
  ManifestTemplateCache& cache = _GetManifestTemplateCache();

  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    const auto cached = cache.reflectors_by_manifest_path.find(cache_key);
    if (cached != cache.reflectors_by_manifest_path.end()) {
      *out_reflector = cached->second;
      return true;
    }
  }

  AlgorithmContainerManifest manifest{};
  if (!LoadAlgorithmContainerManifestFromJsonFile(path, &manifest, out_error_message)) {
    return false;
  }

  AlgorithmReflector reflector{};
  if (!CreateAlgorithmReflectorFromManifest(manifest, &reflector, out_error_message)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto [it, inserted] = cache.reflectors_by_manifest_path.emplace(cache_key, std::move(reflector));
    (void)inserted;
    *out_reflector = it->second;
  }

  return true;
}

bool CreateAlgorithmContainersFromManifestName(
  const std::string& manifest_name,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message) {
  fs::path manifest_path;
  if (_ResolveManifestPathByName(manifest_name, &manifest_path, out_error_message) != ManifestLookupResult::Found) {
    return false;
  }
  return CreateAlgorithmContainersFromManifestFile(manifest_path.generic_string(), out_container_set, out_error_message);
}

bool CreateAlgorithmReflectorFromManifestName(
  const std::string& manifest_name,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  fs::path manifest_path;
  if (_ResolveManifestPathByName(manifest_name, &manifest_path, out_error_message) != ManifestLookupResult::Found) {
    return false;
  }
  return CreateAlgorithmReflectorFromManifestFile(manifest_path.generic_string(), out_reflector, out_error_message);
}

bool TryCreateAlgorithmReflectorFromAlgorithmName(
  const std::string& algorithm_name,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  fs::path manifest_path;
  const ManifestLookupResult lookup_result =
    _ResolveManifestPathByName(algorithm_name, &manifest_path, out_error_message);
  if (lookup_result == ManifestLookupResult::NotFound) {
    out_reflector->Clear();
    out_reflector->algorithm_name = _Trim(algorithm_name);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (lookup_result != ManifestLookupResult::Found) {
    return false;
  }

  return CreateAlgorithmReflectorFromManifestFile(manifest_path.generic_string(), out_reflector, out_error_message);
}

}  // namespace algorithm
