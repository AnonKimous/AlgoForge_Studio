#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/catalog/algorithm_vk_exec_support_detail.h"
#include "algomanager/catalog/algorithm_intervention_support_detail.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/catalog/algorithm_package_paths.h"
#include "algomanager/bridge/algorithm_types.h"
#include "algomanager/catalog/algorithm_json_utils.h"
#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/runtime_environment.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include "cJSON.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace algomanager { namespace algocatalog {

namespace {

namespace fs = std::filesystem;

struct AlgorithmObjectMountMetadataCacheEntry {
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_template{};
  algomanager::AlgorithmTickLifetime tick_lifetime{algomanager::AlgorithmTickLifetime::Continuous};
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> runtime_transfer_map{};
  bool has_runtime_transfer_map{false};
  std::vector<std::string> pipeline_external_write_reset_container_names{};
  bool valid{false};
};

bool _LoadContainerSetFromPackageJson(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmContainerSet>* out_container_set,
  std::string* out_error_message);

bool _ResolveSingleRuntimeContainerReferenceName(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& raw_name,
  const std::string& field_name,
  const std::string& package_path,
  std::string* out_container_name,
  std::string* out_error_message);

bool _LoadPackageRuntimeLifetime(
  const algorithm::AlgorithmPackageLocation& package_location,
  algomanager::AlgorithmTickLifetime* out_tick_lifetime,
  std::string* out_error_message);

bool _LoadPackageRuntimePipelineExternalWriteResetContainers(
  const algorithm::AlgorithmPackageLocation& package_location,
  const algorithm::AlgorithmContainerSet& container_set,
  std::vector<std::string>* out_container_names,
  std::string* out_error_message);

bool _LoadPackageRuntimeTransferMap(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  bool* out_has_transfer_map,
  std::string* out_error_message);

bool _HasDeclaredContainerNameConflict(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& container_name);

bool _ShouldEmitPipelineRunnerProbe(const std::string& algorithm_name) {
  return algorithm_name.find("v4a10_teapot_pbr_demo") != std::string::npos ||
    algorithm_name.find("v4a16_fireworks_pipeline_demo") != std::string::npos;
}

bool _HasDeclaredContainerNameConflict(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& container_name) {
  return algorithm::FindAlgorithmContainer(container_set, container_name) != nullptr;
}

void _AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line) {
  const fs::path path = algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() /
    file_name;
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

std::string _BuildAlgorithmObjectMountMetadataCacheKey(
  const algorithm::AlgorithmPackageLocation& package_location) {
  return package_location.manifest_path.generic_string() + "|" +
    package_location.plugin_module_path.generic_string() + "|" +
    package_location.package_file_path.generic_string();
}

bool _BuildAlgorithmObjectMountMetadata(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmObjectMountMetadataCacheEntry* out_entry,
  std::string* out_error_message) {
  if (!out_entry) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject mount metadata output pointer is null.";
    }
    return false;
  }

  *out_entry = {};
  if (!_LoadContainerSetFromPackageJson(package_location, &out_entry->container_template, out_error_message)) {
    return false;
  }
  if (!out_entry->container_template) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject mount metadata container template is unavailable.";
    }
    return false;
  }
  if (!_LoadPackageRuntimeLifetime(package_location, &out_entry->tick_lifetime, out_error_message)) {
    return false;
  }
  algorithm::AlgorithmRuntimeTransferMap transfer_map{};
  if (!_LoadPackageRuntimeTransferMap(
        package_location,
        &transfer_map,
        &out_entry->has_runtime_transfer_map,
        out_error_message)) {
    return false;
  }
  if (out_entry->has_runtime_transfer_map) {
    out_entry->runtime_transfer_map =
      std::make_shared<algorithm::AlgorithmRuntimeTransferMap>(std::move(transfer_map));
  }
  if (!_LoadPackageRuntimePipelineExternalWriteResetContainers(
        package_location,
        *out_entry->container_template,
        &out_entry->pipeline_external_write_reset_container_names,
        out_error_message)) {
    return false;
  }
  out_entry->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _LoadAlgorithmObjectMountMetadata(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmObjectMountMetadataCacheEntry* out_entry,
  std::string* out_error_message) {
  if (!out_entry) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject mount metadata output pointer is null.";
    }
    return false;
  }

  static std::mutex cache_mutex;
  static std::unordered_map<std::string, AlgorithmObjectMountMetadataCacheEntry> cache;
  const std::string cache_key = _BuildAlgorithmObjectMountMetadataCacheKey(package_location);

  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto found = cache.find(cache_key);
    if (found != cache.end() && found->second.valid) {
      *out_entry = found->second;
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
  }

  AlgorithmObjectMountMetadataCacheEntry built_entry{};
  if (!_BuildAlgorithmObjectMountMetadata(package_location, &built_entry, out_error_message)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache[cache_key] = built_entry;
  }
  *out_entry = std::move(built_entry);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
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
  if (out_error_message) {
    *out_error_message =
      "Unsupported precision '" + std::string(text) + "' in package JSON: " + package_path;
  }
  return false;
}

bool _ResolveContainerScalarBits(
  const cJSON* root,
  const cJSON* container_item,
  const std::string& package_path,
  uint32_t* out_bits,
  std::string* out_error_message) {
  if (!out_bits) {
    if (out_error_message) {
      *out_error_message = "Container scalar precision output pointer is null.";
    }
    return false;
  }

  uint32_t default_bits = 32u;
  const cJSON* global_cfg = cJSON_GetObjectItemCaseSensitive(root, "globalCfg");
  if (global_cfg) {
    if (!cJSON_IsObject(global_cfg)) {
      if (out_error_message) {
        *out_error_message = "Package globalCfg must be an object: " + package_path;
      }
      return false;
    }
    const cJSON* default_precision_item =
      cJSON_GetObjectItemCaseSensitive(global_cfg, "defaultPrecision");
    if (default_precision_item) {
      if (!cJSON_IsString(default_precision_item) || !default_precision_item->valuestring) {
        if (out_error_message) {
          *out_error_message = "Package defaultPrecision must be a string: " + package_path;
        }
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

  *out_bits = default_bits;
  if (!container_item || !cJSON_IsObject(container_item)) {
    return true;
  }

  const cJSON* precision_item = cJSON_GetObjectItemCaseSensitive(container_item, "precision");
  if (!precision_item) {
    precision_item = cJSON_GetObjectItemCaseSensitive(container_item, "precise");
  }
  if (!precision_item) {
    return true;
  }
  if (!cJSON_IsString(precision_item) || !precision_item->valuestring) {
    if (out_error_message) {
      *out_error_message = "Container precise must be a string: " + package_path;
    }
    return false;
  }
  return _ParseScalarPrecisionBits(
    precision_item->valuestring,
    package_path,
    out_bits,
    out_error_message);
}

bool _LoadPackageRuntimeLifetime(
  const algorithm::AlgorithmPackageLocation& package_location,
  algomanager::AlgorithmTickLifetime* out_tick_lifetime,
  std::string* out_error_message) {
  if (!out_tick_lifetime) {
    if (out_error_message) {
      *out_error_message = "AlgorithmTickLifetime output pointer is null.";
    }
    return false;
  }

  *out_tick_lifetime = algomanager::AlgorithmTickLifetime::Continuous;

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for runtime lifetime.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
  if (runtime) {
    if (!cJSON_IsObject(runtime)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package runtime section is invalid: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* launch_once = cJSON_GetObjectItemCaseSensitive(runtime, "launchOnce");
    if (launch_once) {
      if (!cJSON_IsBool(launch_once)) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package runtime launchOnce must be boolean: " + package_json_path.generic_string();
        }
        return false;
      }
      if (cJSON_IsTrue(launch_once)) {
        *out_tick_lifetime = algomanager::AlgorithmTickLifetime::LaunchOnceThenHold;
      }
    }
  }

  cJSON_Delete(root);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

std::string _TrimRuntimeToken(std::string value) {
  const auto is_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };
  const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  if (begin >= end) {
    return {};
  }
  return std::string(begin, end);
}

std::string _TrimRuntimeAliasName(std::string value) {
  value = _TrimRuntimeToken(std::move(value));
  const std::string::size_type slash = value.find('/');
  if (slash == std::string::npos) {
    return value;
  }
  return _TrimRuntimeToken(value.substr(0, slash));
}

std::vector<std::string> _SplitRuntimeTokenList(const std::string& text) {
  std::vector<std::string> tokens{};
  size_t cursor = 0u;
  while (cursor <= text.size()) {
    const std::string::size_type comma = text.find(',', cursor);
    tokens.push_back(_TrimRuntimeToken(
      text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor)));
    if (comma == std::string::npos) {
      break;
    }
    cursor = comma + 1u;
  }
  return tokens;
}

bool _ParseRuntimeStageLinkKey(
  const std::string& key,
  std::string* out_source_stage_name,
  std::string* out_target_stage_name,
  std::string* out_error_message) {
  if (!out_source_stage_name || !out_target_stage_name) {
    if (out_error_message) {
      *out_error_message = "Runtime stage link output pointers are null.";
    }
    return false;
  }

  const std::string::size_type arrow = key.find("->");
  if (arrow == std::string::npos || key.find("->", arrow + 2u) != std::string::npos) {
    if (out_error_message) {
      *out_error_message = "Runtime stage link key must use the form source->target: " + key;
    }
    return false;
  }

  *out_source_stage_name = _TrimRuntimeToken(key.substr(0, arrow));
  *out_target_stage_name = _TrimRuntimeToken(key.substr(arrow + 2u));
  if (out_source_stage_name->empty() || out_target_stage_name->empty()) {
    if (out_error_message) {
      *out_error_message = "Runtime stage link key contains an empty stage name: " + key;
    }
    return false;
  }
  return true;
}

bool _AppendRuntimeTransferBinding(
  algorithm::AlgorithmRuntimeTransferEdge* out_edge,
  algorithm::AlgorithmRuntimeTransferBinding binding,
  std::string* out_error_message) {
  if (!out_edge) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge output pointer is null.";
    }
    return false;
  }
  if (binding.from_name.empty() || binding.to_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer binding is missing a source or target name.";
    }
    return false;
  }
  for (const algorithm::AlgorithmRuntimeTransferBinding& existing : out_edge->bindings) {
    if (existing.from_name == binding.from_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding source is duplicated: " + binding.from_name;
      }
      return false;
    }
    if (existing.to_name == binding.to_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding target is duplicated: " + binding.to_name;
      }
      return false;
    }
  }
  out_edge->bindings.push_back(std::move(binding));
  return true;
}

bool _ValidateRuntimeTransferEdgeLinearity(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const algorithm::AlgorithmRuntimeTransferEdge& edge,
  const std::string& package_path,
  std::string* out_error_message) {
  if (edge.source_stage_name == edge.target_stage_name) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge cannot loop to itself: " + edge.source_stage_name +
        "->" + edge.target_stage_name + " in " + package_path;
    }
    return false;
  }

  for (const algorithm::AlgorithmRuntimeTransferEdge& existing : transfer_map.stage_links) {
    if (existing.source_stage_name == edge.source_stage_name &&
        existing.target_stage_name == edge.target_stage_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer edge is duplicated: " +
          edge.source_stage_name + "->" + edge.target_stage_name;
      }
      return false;
    }
    if (existing.source_stage_name == edge.source_stage_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map must be linear: stage '" + edge.source_stage_name +
          "' already maps to next stage '" + existing.target_stage_name + "' in " + package_path;
      }
      return false;
    }
    if (existing.target_stage_name == edge.target_stage_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map must be linear: stage '" + edge.target_stage_name +
          "' already receives data from '" + existing.source_stage_name + "' in " + package_path;
      }
      return false;
    }
  }

  return true;
}

bool _LoadRuntimeTransferBindingsFromObject(
  const cJSON* bindings_object,
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferEdge* out_edge,
  std::string* out_error_message) {
  if (!bindings_object || !cJSON_IsObject(bindings_object) || !out_edge) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer bindings object is invalid: " + package_path;
    }
    return false;
  }

  for (const cJSON* child = bindings_object->child; child; child = child->next) {
    if (!child->string || !*child->string) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding source name is empty: " + package_path;
      }
      return false;
    }

    algorithm::AlgorithmRuntimeTransferBinding binding{};
    if (!_ResolveSingleRuntimeContainerReferenceName(
          container_set,
          child->string,
          "transfer binding source",
          package_path,
          &binding.from_name,
          out_error_message)) {
      return false;
    }
    if (cJSON_IsString(child) && child->valuestring) {
      if (!_ResolveSingleRuntimeContainerReferenceName(
            container_set,
            child->valuestring,
            "transfer binding target",
            package_path,
            &binding.to_name,
            out_error_message)) {
        return false;
      }
    } else if (cJSON_IsObject(child)) {
      std::string raw_target_name = json_utils::GetStringField(child, "to");
      if (raw_target_name.empty()) {
        raw_target_name = json_utils::GetStringField(child, "target");
      }
      if (!_ResolveSingleRuntimeContainerReferenceName(
            container_set,
            raw_target_name,
            "transfer binding target",
            package_path,
            &binding.to_name,
            out_error_message)) {
        return false;
      }
      binding.required = json_utils::GetBoolField(child, "required", true);
    } else {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding value is invalid: " + package_path;
      }
      return false;
    }

    if (!_AppendRuntimeTransferBinding(out_edge, std::move(binding), out_error_message)) {
      return false;
    }
  }

  return true;
}

bool _LoadRuntimeTransferBindingsFromArray(
  const cJSON* bindings_array,
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferEdge* out_edge,
  std::string* out_error_message) {
  if (!bindings_array || !cJSON_IsArray(bindings_array) || !out_edge) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer bindings array is invalid: " + package_path;
    }
    return false;
  }

  const int binding_count = cJSON_GetArraySize(bindings_array);
  for (int i = 0; i < binding_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(bindings_array, i);
    if (!item || !cJSON_IsObject(item)) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding item is invalid: " + package_path;
      }
      return false;
    }

    algorithm::AlgorithmRuntimeTransferBinding binding{};
    std::string raw_source_name = json_utils::GetStringField(item, "from");
    if (raw_source_name.empty()) {
      raw_source_name = json_utils::GetStringField(item, "source");
    }
    if (!_ResolveSingleRuntimeContainerReferenceName(
          container_set,
          raw_source_name,
          "transfer binding source",
          package_path,
          &binding.from_name,
          out_error_message)) {
      return false;
    }
    std::string raw_target_name = json_utils::GetStringField(item, "to");
    if (raw_target_name.empty()) {
      raw_target_name = json_utils::GetStringField(item, "target");
    }
    if (!_ResolveSingleRuntimeContainerReferenceName(
          container_set,
          raw_target_name,
          "transfer binding target",
          package_path,
          &binding.to_name,
          out_error_message)) {
      return false;
    }
    binding.required = json_utils::GetBoolField(item, "required", true);
    if (!_AppendRuntimeTransferBinding(out_edge, std::move(binding), out_error_message)) {
      return false;
    }
  }

  return true;
}

bool _LoadRuntimeTransferEdgeFromObject(
  const cJSON* item,
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  std::string* out_error_message) {
  if (!item || !cJSON_IsObject(item) || !out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge item is invalid: " + package_path;
    }
    return false;
  }

  algorithm::AlgorithmRuntimeTransferEdge edge{};
  edge.source_stage_name = json_utils::GetStringField(item, "from_stage");
  if (edge.source_stage_name.empty()) {
    edge.source_stage_name = json_utils::GetStringField(item, "source_stage");
  }
  edge.target_stage_name = json_utils::GetStringField(item, "to_stage");
  if (edge.target_stage_name.empty()) {
    edge.target_stage_name = json_utils::GetStringField(item, "target_stage");
  }
  if (edge.source_stage_name.empty() || edge.target_stage_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge is missing source or target stage: " + package_path;
    }
    return false;
  }

  const cJSON* bindings = cJSON_GetObjectItemCaseSensitive(item, "bindings");
  if (!bindings) {
    bindings = cJSON_GetObjectItemCaseSensitive(item, "map");
  }
  if (!bindings) {
    bindings = cJSON_GetObjectItemCaseSensitive(item, "links");
  }
  if (bindings) {
    if (cJSON_IsObject(bindings)) {
      if (!_LoadRuntimeTransferBindingsFromObject(
            bindings,
            container_set,
            package_path,
            &edge,
            out_error_message)) {
        return false;
      }
    } else if (cJSON_IsArray(bindings)) {
      if (!_LoadRuntimeTransferBindingsFromArray(
            bindings,
            container_set,
            package_path,
            &edge,
            out_error_message)) {
        return false;
      }
    } else {
      if (out_error_message) {
        *out_error_message = "Runtime transfer edge bindings have an invalid type: " + package_path;
      }
      return false;
    }
  }

  if (!_ValidateRuntimeTransferEdgeLinearity(*out_transfer_map, edge, package_path, out_error_message)) {
    return false;
  }

  out_transfer_map->stage_links.push_back(std::move(edge));
  return true;
}

bool _LoadRuntimeTransferEdgeFromKeyValue(
  const std::string& edge_key,
  const cJSON* value,
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer map output pointer is null.";
    }
    return false;
  }

  std::string source_stage_name;
  std::string target_stage_name;
  if (!_ParseRuntimeStageLinkKey(edge_key, &source_stage_name, &target_stage_name, out_error_message)) {
    return false;
  }

  algorithm::AlgorithmRuntimeTransferEdge edge{};
  edge.source_stage_name = std::move(source_stage_name);
  edge.target_stage_name = std::move(target_stage_name);
  if (value) {
    if (cJSON_IsObject(value)) {
      if (!_LoadRuntimeTransferBindingsFromObject(
            value,
            container_set,
            package_path,
            &edge,
            out_error_message)) {
        return false;
      }
    } else if (cJSON_IsArray(value)) {
      if (!_LoadRuntimeTransferBindingsFromArray(
            value,
            container_set,
            package_path,
            &edge,
            out_error_message)) {
        return false;
      }
    } else {
      if (out_error_message) {
        *out_error_message = "Runtime transfer edge payload is invalid: " + package_path;
      }
      return false;
    }
  }

  if (!_ValidateRuntimeTransferEdgeLinearity(*out_transfer_map, edge, package_path, out_error_message)) {
    return false;
  }

  out_transfer_map->stage_links.push_back(std::move(edge));
  return true;
}

bool _LoadPackageRuntimeTransferMap(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  bool* out_has_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "AlgorithmRuntimeTransferMap output pointer is null.";
    }
    return false;
  }

  out_transfer_map->Clear();
  if (out_has_transfer_map) {
    *out_has_transfer_map = false;
  }

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for runtime transfer map.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
  if (!runtime) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsObject(runtime)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime section is invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* pipeline = cJSON_GetObjectItemCaseSensitive(runtime, "pipeline");
  if (!pipeline) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsObject(pipeline)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline section is invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmContainerSet> runtime_container_set{};
  std::string runtime_container_error_message;
  if (!_LoadContainerSetFromPackageJson(
        package_location,
        &runtime_container_set,
        &runtime_container_error_message)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = runtime_container_error_message.empty()
        ? ("Failed to load runtime pipeline container aliases: " + package_json_path.generic_string())
        : std::move(runtime_container_error_message);
    }
    return false;
  }
  if (!runtime_container_set) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Runtime pipeline container set is unavailable: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* circular_tick = cJSON_GetObjectItemCaseSensitive(pipeline, "supportsCircularTick");
  if (!circular_tick) {
    circular_tick = cJSON_GetObjectItemCaseSensitive(pipeline, "circularTick");
  }
  if (circular_tick) {
    if (!cJSON_IsBool(circular_tick)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package runtime pipeline supportsCircularTick must be boolean: " +
          package_json_path.generic_string();
      }
      return false;
    }
    out_transfer_map->supports_circular_tick = cJSON_IsTrue(circular_tick);
  }

  const cJSON* mappings = cJSON_GetObjectItemCaseSensitive(pipeline, "mappings");
  if (!mappings) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline is missing mappings: " + package_json_path.generic_string();
    }
    return false;
  }

  out_transfer_map->algorithm_name = package_location.algorithm_name;

  if (cJSON_IsObject(mappings)) {
    for (const cJSON* child = mappings->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Runtime transfer mapping key is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      if (!_LoadRuntimeTransferEdgeFromKeyValue(
            child->string,
            child,
            *runtime_container_set,
            package_json_path.generic_string(),
            out_transfer_map,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsArray(mappings)) {
    const int mapping_count = cJSON_GetArraySize(mappings);
    for (int i = 0; i < mapping_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(mappings, i);
      if (!_LoadRuntimeTransferEdgeFromObject(
            item,
            *runtime_container_set,
            package_json_path.generic_string(),
            out_transfer_map,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline mappings are invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON_Delete(root);
  out_transfer_map->valid = true;
  if (out_has_transfer_map) {
    *out_has_transfer_map = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _ResolveSingleRuntimeContainerReferenceName(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& raw_name,
  const std::string& field_name,
  const std::string& package_path,
  std::string* out_container_name,
  std::string* out_error_message) {
  if (!out_container_name) {
    if (out_error_message) {
      *out_error_message = "Runtime container reference output pointer is null.";
    }
    return false;
  }
  out_container_name->clear();
  if (!algorithm::TryResolveSingleAlgorithmContainerReferenceName(
        container_set,
        raw_name,
        out_container_name)) {
    if (out_error_message) {
      *out_error_message =
        "Runtime " + field_name + " must resolve to exactly one container name: " +
        raw_name + " (" + package_path + ")";
    }
    return false;
  }
  return true;
}

bool _LoadPackageRuntimePipelineExternalWriteResetContainers(
  const algorithm::AlgorithmPackageLocation& package_location,
  const algorithm::AlgorithmContainerSet& container_set,
  std::vector<std::string>* out_container_names,
  std::string* out_error_message) {
  if (!out_container_names) {
    if (out_error_message) {
      *out_error_message = "Pipeline external-write reset container list output pointer is null.";
    }
    return false;
  }

  out_container_names->clear();

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for pipeline external-write reset containers.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
  if (!runtime) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsObject(runtime)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime section is invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* pipeline = cJSON_GetObjectItemCaseSensitive(runtime, "pipeline");
  if (!pipeline) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsObject(pipeline)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline section is invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* reset_containers =
    cJSON_GetObjectItemCaseSensitive(pipeline, "externalWriteResetContainers");
  if (!reset_containers) {
    reset_containers = cJSON_GetObjectItemCaseSensitive(pipeline, "clearAfterTickContainers");
  }
  if (!reset_containers) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsArray(reset_containers)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message =
        "Package runtime pipeline externalWriteResetContainers must be an array: " +
        package_json_path.generic_string();
    }
    return false;
  }

  std::unordered_set<std::string> seen_container_names{};
  const int reset_container_count = cJSON_GetArraySize(reset_containers);
  out_container_names->reserve(reset_container_count > 0 ? static_cast<size_t>(reset_container_count) : 0u);
  for (int i = 0; i < reset_container_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(reset_containers, i);
    if (!item || !cJSON_IsString(item) || !item->valuestring || !*item->valuestring) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message =
          "Package runtime pipeline externalWriteResetContainers entry must be a non-empty string: " +
          package_json_path.generic_string();
      }
      return false;
    }

    std::string container_name{};
    if (!_ResolveSingleRuntimeContainerReferenceName(
          container_set,
          item->valuestring,
          "pipeline externalWriteResetContainers entry",
          package_json_path.generic_string(),
          &container_name,
          out_error_message)) {
      cJSON_Delete(root);
      return false;
    }
    if (!seen_container_names.insert(container_name).second) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message =
          "Package runtime pipeline externalWriteResetContainers contains a duplicated container name: " +
          container_name;
      }
      return false;
    }

    if (!algorithm::FindAlgorithmContainer(container_set, container_name)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message =
          "Package runtime pipeline externalWriteResetContainers references an unknown container '" +
          container_name + "' in " + package_json_path.generic_string();
      }
      return false;
    }

    out_container_names->push_back(container_name);
  }

  cJSON_Delete(root);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _AppendContainer(
  const std::string& container_name,
  algorithm::AlgorithmContainerStorageKind storage_kind,
  uint32_t element_count,
  uint32_t element_stride,
  algorithm::AlgorithmContainerSet* out_container_set) {
  algorithm::AlgorithmContainer container{};
  container.name = container_name;
  container.storage_kind = storage_kind;
  container.element_count = element_count;
  container.element_stride = element_stride;
  container.bytes.resize(static_cast<size_t>(element_count) * static_cast<size_t>(element_stride));

  switch (storage_kind) {
    case algorithm::AlgorithmContainerStorageKind::Array:
      out_container_set->arrays.push_back(std::move(container));
      break;
    case algorithm::AlgorithmContainerStorageKind::TemporaryRegister:
      out_container_set->temporary_registers.push_back(std::move(container));
      break;
    case algorithm::AlgorithmContainerStorageKind::TemporaryCache:
      out_container_set->temporary_caches.push_back(std::move(container));
      break;
  }
  return true;
}

bool _AppendHiddenInterventionSignalContainer(algorithm::AlgorithmContainerSet* out_container_set) {
  if (!out_container_set) {
    return false;
  }

  algorithm::AlgorithmContainer signal_container{};
  signal_container.name = "__algorithm_intervention_signal";
  signal_container.storage_kind = algorithm::AlgorithmContainerStorageKind::TemporaryCache;
  signal_container.element_count = 1u;
  signal_container.element_stride = sizeof(uint32_t);
  signal_container.hidden = true;
  signal_container.bytes.resize(sizeof(uint32_t));
  out_container_set->hidden_containers.push_back(std::move(signal_container));
  return true;
}

std::string _BuildStandardContainerLayoutKey(const algorithm::AlgorithmContainerSet& container_set) {
  std::vector<std::string> tokens{};
  tokens.reserve(container_set.temporary_registers.size() + container_set.arrays.size());

  for (const algorithm::AlgorithmContainer& container : container_set.temporary_registers) {
    tokens.push_back(
      "v:" + container.name + ":" +
      std::to_string(container.element_count) + ":" +
      std::to_string(container.element_stride));
  }
  for (const algorithm::AlgorithmContainer& container : container_set.arrays) {
    tokens.push_back(
      "a:" + container.name + ":" +
      std::to_string(container.element_count) + ":" +
      std::to_string(container.element_stride));
  }

  if (tokens.empty()) {
    return {};
  }

  std::sort(tokens.begin(), tokens.end());
  std::string key{"standard:"};
  for (const std::string& token : tokens) {
    key.append(token);
    key.push_back('|');
  }
  return key;
}

bool _LoadContainerAliasesFromJson(
  const cJSON* container_section,
  const std::string& package_path,
  algorithm::AlgorithmContainerSet* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) {
    if (out_error_message) {
      *out_error_message = "Container alias target container set is null.";
    }
    return false;
  }

  const cJSON* aliases = cJSON_GetObjectItemCaseSensitive(container_section, "aliases");
  if (!aliases) {
    return true;
  }
  if (!cJSON_IsArray(aliases)) {
    if (out_error_message) {
      *out_error_message = "Package container aliases must be an array: " + package_path;
    }
    return false;
  }

  const int alias_count = cJSON_GetArraySize(aliases);
  for (int i = 0; i < alias_count; ++i) {
    const cJSON* alias_item = cJSON_GetArrayItem(aliases, i);
    if (!alias_item || !cJSON_IsString(alias_item) || !alias_item->valuestring) {
      if (out_error_message) {
        *out_error_message = "Package container alias entry must be a string: " + package_path;
      }
      return false;
    }

    const std::string alias_text = alias_item->valuestring;
    const std::string::size_type colon = alias_text.find(':');
    if (colon == std::string::npos || alias_text.find(':', colon + 1u) != std::string::npos) {
      if (out_error_message) {
        *out_error_message =
          "Package container alias must use the form source1,source2:alias or source:view1,view2: " + alias_text +
          " (" + package_path + ")";
      }
      return false;
    }

    const std::vector<std::string> left_tokens = _SplitRuntimeTokenList(alias_text.substr(0, colon));
    const std::vector<std::string> right_tokens = _SplitRuntimeTokenList(alias_text.substr(colon + 1u));
    if (left_tokens.empty() || right_tokens.empty()) {
      if (out_error_message) {
        *out_error_message = "Package container alias must contain both source and target names: " + package_path;
      }
      return false;
    }
    if (left_tokens.size() > 1u && right_tokens.size() > 1u) {
      if (out_error_message) {
        *out_error_message =
          "Package container alias cannot expand multiple sources into multiple targets at once: " + alias_text +
          " (" + package_path + ")";
      }
      return false;
    }

    const auto is_name_conflict = [&](const std::string& name) {
      return algorithm::FindAlgorithmContainer(*out_container_set, name) ||
        out_container_set->container_aliases_by_name.find(name) != out_container_set->container_aliases_by_name.end();
    };

    if (left_tokens.size() == 1u && right_tokens.size() > 1u) {
      std::string resolved_source_name{};
      if (!algorithm::TryResolveSingleAlgorithmContainerReferenceName(
            *out_container_set,
            left_tokens.front(),
            &resolved_source_name)) {
        if (out_error_message) {
          *out_error_message =
            "Package container view alias references an unknown container name: " + left_tokens.front() +
            " (" + package_path + ")";
        }
        return false;
      }
      if (resolved_source_name.empty()) {
        if (out_error_message) {
          *out_error_message =
            "Package container view alias source is empty: " + package_path;
        }
        return false;
      }

      std::unordered_set<std::string> unique_alias_names{};
      for (const std::string& alias_name_raw : right_tokens) {
        const std::string alias_name = _TrimRuntimeAliasName(alias_name_raw);
        if (alias_name.empty()) {
          if (out_error_message) {
            *out_error_message = "Package container alias name is empty: " + package_path;
          }
          return false;
        }
        if (is_name_conflict(alias_name) || !unique_alias_names.insert(alias_name).second) {
          if (out_error_message) {
            *out_error_message =
              "Package container alias conflicts with an existing container or alias name: " + alias_name +
              " (" + package_path + ")";
          }
          return false;
        }
        out_container_set->container_aliases_by_name.emplace(alias_name, std::vector<std::string>{resolved_source_name});
      }
      continue;
    }

    const std::string alias_name = _TrimRuntimeAliasName(right_tokens.front());
    if (alias_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Package container alias name is empty: " + package_path;
      }
      return false;
    }
    if (is_name_conflict(alias_name)) {
      if (out_error_message) {
        *out_error_message =
          "Package container alias conflicts with an existing container or alias name: " + alias_name +
          " (" + package_path + ")";
      }
      return false;
    }

    std::vector<std::string> resolved_container_names{};
    std::unordered_set<std::string> unique_container_names{};
    for (const std::string& token_raw : left_tokens) {
      const std::string token = _TrimRuntimeToken(token_raw);
      if (token.empty()) {
        if (out_error_message) {
          *out_error_message =
            "Package container alias contains an empty source name: " + alias_text +
            " (" + package_path + ")";
        }
        return false;
      }

      std::string resolved_name{};
      if (!algorithm::TryResolveSingleAlgorithmContainerReferenceName(
            *out_container_set,
            token,
            &resolved_name)) {
        if (out_error_message) {
          *out_error_message =
            "Package container alias references an unknown or grouped container name: " + token +
            " (" + package_path + ")";
        }
        return false;
      }
      if (!unique_container_names.insert(resolved_name).second) {
        if (out_error_message) {
          *out_error_message =
            "Package container alias repeats the same container more than once: " + resolved_name +
            " (" + package_path + ")";
        }
        return false;
      }
      resolved_container_names.push_back(std::move(resolved_name));
    }

    if (resolved_container_names.empty()) {
      if (out_error_message) {
        *out_error_message = "Package container alias has no source containers: " + package_path;
      }
      return false;
    }
    out_container_set->container_aliases_by_name.emplace(alias_name, std::move(resolved_container_names));
  }

  return true;
}

bool _ExpandResolvedContainerReferenceNames(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::vector<std::string>& raw_names,
  const std::string& field_name,
  const std::string& package_path,
  std::vector<std::string>* out_container_names,
  std::string* out_error_message) {
  if (!out_container_names) {
    if (out_error_message) {
      *out_error_message = "Container reference expansion output vector is null.";
    }
    return false;
  }
  out_container_names->clear();

  for (const std::string& raw_name : raw_names) {
    std::vector<std::string> resolved_names{};
    if (!algorithm::TryResolveAlgorithmContainerReferenceNames(
          container_set,
          raw_name,
          &resolved_names)) {
      if (out_error_message) {
        *out_error_message =
          "Package " + field_name + " references an unknown container name '" + raw_name +
          "': " + package_path;
      }
      return false;
    }
    out_container_names->insert(out_container_names->end(), resolved_names.begin(), resolved_names.end());
  }

  if (out_container_names->empty()) {
    if (out_error_message) {
      *out_error_message =
        "Package " + field_name + " resolves to an empty container list: " + package_path;
    }
    return false;
  }
  return true;
}

bool _LoadContainerSetFromPackageJson(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmContainerSet>* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) {
    if (out_error_message) {
      *out_error_message = "Package container set output pointer is null.";
    }
    return false;
  }

  out_container_set->reset();

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* container_section = cJSON_GetObjectItemCaseSensitive(root, "container");
  if (!container_section || !cJSON_IsObject(container_section)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package JSON is missing container section: " + package_json_path.generic_string();
    }
    return false;
  }

  auto container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
  container_set->algorithm_name = package_location.algorithm_name;

  const cJSON* variable_item = cJSON_GetObjectItemCaseSensitive(container_section, "variable");
  if (cJSON_IsNumber(variable_item)) {
    const uint32_t variable_count = json_utils::GetUintField(container_section, "variable");
    uint32_t scalar_bits = 32u;
    if (!_ResolveContainerScalarBits(root, nullptr, package_json_path.generic_string(), &scalar_bits, out_error_message)) {
      cJSON_Delete(root);
      return false;
    }
    const uint32_t scalar_bytes = scalar_bits / 8u;
    for (uint32_t i = 0; i < variable_count; ++i) {
      const std::string container_name = "v" + std::to_string(i + 1u);
      if (_HasDeclaredContainerNameConflict(*container_set, container_name)) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message =
            "Package variable container name conflicts with an existing container: " +
            container_name + " (" + package_json_path.generic_string() + ")";
        }
        return false;
      }
      if (!_AppendContainer(
            container_name,
            algorithm::AlgorithmContainerStorageKind::TemporaryRegister,
            1u,
            scalar_bytes,
            container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsObject(variable_item)) {
    for (const cJSON* child = variable_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package variable container name is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      uint32_t scalar_bits = 32u;
      if (!_ResolveContainerScalarBits(root, child, package_json_path.generic_string(), &scalar_bits, out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
      const uint32_t scalar_bytes = scalar_bits / 8u;
      const uint32_t element_count = cJSON_IsObject(child) ? std::max<uint32_t>(1u, json_utils::GetUintField(child, "count", 1u)) : 1u;
      const uint32_t tuple_width = json_utils::GetTupleWidthField(child);
      const uint32_t element_stride = tuple_width * scalar_bytes;
      if (_HasDeclaredContainerNameConflict(*container_set, child->string)) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message =
            "Package variable container name conflicts with an existing container: " +
            std::string(child->string) + " (" + package_json_path.generic_string() + ")";
        }
        return false;
      }
      if (!_AppendContainer(child->string, algorithm::AlgorithmContainerStorageKind::TemporaryRegister, element_count, element_stride, container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  }

  const cJSON* variable_array_item = cJSON_GetObjectItemCaseSensitive(container_section, "variableArray");
  if (cJSON_IsNumber(variable_array_item)) {
    const uint32_t variable_array_count = json_utils::GetUintField(container_section, "variableArray");
    uint32_t scalar_bits = 32u;
    if (!_ResolveContainerScalarBits(root, nullptr, package_json_path.generic_string(), &scalar_bits, out_error_message)) {
      cJSON_Delete(root);
      return false;
    }
    const uint32_t scalar_bytes = scalar_bits / 8u;
    for (uint32_t i = 0; i < variable_array_count; ++i) {
      const std::string container_name = "a" + std::to_string(i + 1u);
      if (_HasDeclaredContainerNameConflict(*container_set, container_name)) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message =
            "Package array container name conflicts with an existing container: " +
            container_name + " (" + package_json_path.generic_string() + ")";
        }
        return false;
      }
      if (!_AppendContainer(
            container_name,
            algorithm::AlgorithmContainerStorageKind::Array,
            1u,
            scalar_bytes,
            container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsObject(variable_array_item)) {
    for (const cJSON* child = variable_array_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package array container name is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      uint32_t scalar_bits = 32u;
      if (!_ResolveContainerScalarBits(root, child, package_json_path.generic_string(), &scalar_bits, out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
      const uint32_t scalar_bytes = scalar_bits / 8u;
      const uint32_t element_count = cJSON_IsObject(child) ? std::max<uint32_t>(1u, json_utils::GetUintField(child, "count", 1u)) : 1u;
      const uint32_t tuple_width = json_utils::GetTupleWidthField(child);
      const uint32_t element_stride = tuple_width * scalar_bytes;
      if (_HasDeclaredContainerNameConflict(*container_set, child->string)) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message =
            "Package array container name conflicts with an existing container: " +
            std::string(child->string) + " (" + package_json_path.generic_string() + ")";
        }
        return false;
      }
      if (!_AppendContainer(child->string, algorithm::AlgorithmContainerStorageKind::Array, element_count, element_stride, container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  }

  if (!_LoadContainerAliasesFromJson(
        container_section,
        package_json_path.generic_string(),
        container_set.get(),
        out_error_message)) {
    cJSON_Delete(root);
    return false;
  }

  cJSON_Delete(root);
  container_set->standard_layout.layout_kind = "standard";
  container_set->standard_layout.variable_count = static_cast<uint32_t>(container_set->temporary_registers.size());
  container_set->standard_layout.array_count = static_cast<uint32_t>(container_set->arrays.size());
  container_set->standard_layout.variable_prefix = "v";
  container_set->standard_layout.array_prefix = "a";
  container_set->standard_layout.layout_name = _BuildStandardContainerLayoutKey(*container_set);
  *out_container_set = std::move(container_set);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _LoadPackageRuntimeReflector(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for reflector.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* reflector = cJSON_GetObjectItemCaseSensitive(root, "reflector");
  if (!reflector || !cJSON_IsObject(reflector)) {
    cJSON_Delete(root);
    out_reflector->Clear();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  const cJSON* items = cJSON_GetObjectItemCaseSensitive(reflector, "items");
  if (!items) {
    cJSON_Delete(root);
    out_reflector->Clear();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsArray(items)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package reflector is missing items: " + package_json_path.generic_string();
    }
    return false;
  }

  out_reflector->Clear();
  out_reflector->algorithm_name = package_location.algorithm_name;

  const cJSON* refresh_mode_item = cJSON_GetObjectItemCaseSensitive(reflector, "refreshMode");
  if (refresh_mode_item) {
    if (cJSON_IsString(refresh_mode_item) && refresh_mode_item->valuestring) {
      const std::string refresh_mode = refresh_mode_item->valuestring;
      if (refresh_mode == "captureOnce" ||
          refresh_mode == "capture_once" ||
          refresh_mode == "onceAfterCompletion" ||
          refresh_mode == "once_after_completion") {
        out_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
      } else if (refresh_mode == "everyTick" ||
                 refresh_mode == "every_tick") {
        out_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::EveryTick;
      } else {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package reflector refreshMode is invalid: " + package_json_path.generic_string();
        }
        return false;
      }
    } else if (cJSON_IsBool(refresh_mode_item)) {
      out_reflector->refresh_mode = cJSON_IsTrue(refresh_mode_item)
        ? algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion
        : algorithm::AlgorithmReflectionRefreshMode::EveryTick;
    } else {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector refreshMode must be string or boolean: " + package_json_path.generic_string();
      }
      return false;
    }
  }

  const cJSON* function_name_item = cJSON_GetObjectItemCaseSensitive(reflector, "functionName");
  const std::string default_filter_name =
    function_name_item && cJSON_IsString(function_name_item) && function_name_item->valuestring
      ? function_name_item->valuestring
      : std::string{};

  const int item_count = cJSON_GetArraySize(items);
  std::shared_ptr<algorithm::AlgorithmContainerSet> reflector_container_set{};
  std::string reflector_container_error_message;
  if (!_LoadContainerSetFromPackageJson(
        package_location,
        &reflector_container_set,
        &reflector_container_error_message)) {
    if (out_error_message) {
      *out_error_message = reflector_container_error_message.empty()
        ? ("Failed to load package container aliases for reflector: " + package_json_path.generic_string())
        : std::move(reflector_container_error_message);
    }
    cJSON_Delete(root);
    return false;
  }
  if (!reflector_container_set) {
    if (out_error_message) {
      *out_error_message = "Package reflector container set is unavailable: " + package_json_path.generic_string();
    }
    cJSON_Delete(root);
    return false;
  }

  for (int i = 0; i < item_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(items, i);
    if (!item || !cJSON_IsObject(item)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is invalid: " + package_json_path.generic_string();
      }
      return false;
    }

    std::vector<std::string> container_names{};
    std::string reflection_object_name;
    const cJSON* reflect_fun = cJSON_GetObjectItemCaseSensitive(item, "reflectFun");
    const std::string item_filter_name =
      reflect_fun && cJSON_IsString(reflect_fun) && reflect_fun->valuestring
        ? reflect_fun->valuestring
        : default_filter_name;

    if (item_filter_name == "direct") {
      const cJSON* from_item = cJSON_GetObjectItemCaseSensitive(item, "from");
      const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(item, "to");
      std::vector<std::string> from_names{};
      if (!_ExpandResolvedContainerReferenceNames(
            *reflector_container_set,
            json_utils::GetStringList(from_item),
            "reflector.from",
            package_json_path.generic_string(),
            &from_names,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
      const std::vector<std::string> to_names = json_utils::GetStringList(to_item);
      if (from_names.size() != to_names.size() || from_names.empty()) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package reflector direct item must contain same-length from/to lists: " + package_json_path.generic_string();
        }
        return false;
      }

      for (size_t j = 0; j < from_names.size(); ++j) {
        algorithm::AlgorithmReflectionBinding binding{};
        binding.container_names = {from_names[j]};
        binding.reflection_object_name = to_names[j];
        binding.filter_name = item_filter_name;
        out_reflector->container_bindings_by_reflection_object_name.emplace(
          binding.reflection_object_name,
          binding);
        out_reflector->reflection_objects_by_container_name[binding.container_names.front()].push_back(binding);
      }
      continue;
    }

    const cJSON* input = cJSON_GetObjectItemCaseSensitive(item, "input");
    if (!input || !cJSON_IsObject(input)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing input object: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* varity = cJSON_GetObjectItemCaseSensitive(input, "varity");
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(input, "array");
    if (varity) {
      std::vector<std::string> varity_names{};
      if (!_ExpandResolvedContainerReferenceNames(
            *reflector_container_set,
            json_utils::GetStringList(varity),
            "reflector.input.varity",
            package_json_path.generic_string(),
            &varity_names,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
      container_names.insert(container_names.end(), varity_names.begin(), varity_names.end());
    }
    if (array) {
      std::vector<std::string> array_names{};
      if (!_ExpandResolvedContainerReferenceNames(
            *reflector_container_set,
            json_utils::GetStringList(array),
            "reflector.input.array",
            package_json_path.generic_string(),
            &array_names,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
      container_names.insert(container_names.end(), array_names.begin(), array_names.end());
    }
    if (container_names.empty()) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item has an empty input list: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* output = cJSON_GetObjectItemCaseSensitive(item, "output");
    if (!output || !cJSON_IsObject(output)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing output object: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* output_var = cJSON_GetObjectItemCaseSensitive(output, "v");
    const cJSON* output_arr = cJSON_GetObjectItemCaseSensitive(output, "a");
    const cJSON* output_kind = output_var && cJSON_IsObject(output_var)
      ? output_var
      : (output_arr && cJSON_IsObject(output_arr) ? output_arr : nullptr);
    if (output_kind) {
      const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(output_kind, "name");
      if (name_item && cJSON_IsString(name_item) && name_item->valuestring) {
        reflection_object_name = name_item->valuestring;
      }
    }
    if (reflection_object_name.empty()) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing output name: " + package_json_path.generic_string();
      }
      return false;
    }

    algorithm::AlgorithmReflectionBinding binding{};
    binding.container_names = std::move(container_names);
    binding.reflection_object_name = std::move(reflection_object_name);
    binding.filter_name = std::move(item_filter_name);

    out_reflector->container_bindings_by_reflection_object_name.emplace(
      binding.reflection_object_name,
      binding);
    for (const std::string& container_name : binding.container_names) {
      out_reflector->reflection_objects_by_container_name[container_name].push_back(binding);
    }
  }

  cJSON_Delete(root);
  return true;
}

struct PackageDefaultResourceBinding {
  std::string resource_name;
  std::string resource_kind;
  std::string source_path;
  bool required{true};
};

struct PackageDefaultDescriptorValue {
  std::string descriptor_name;
  double scalar_value{0.0};
};

struct PackageDefaultSchema {
  bool valid{false};
  bool has_default_file{false};
  std::vector<PackageDefaultResourceBinding> resource_bindings;
  std::vector<PackageDefaultDescriptorValue> descriptor_values;
  std::string error_message;
};

algomanager::algocatalog::AlgorithmPipelineWrapperStageSpec _LoadWrapperStageSpec(
  const cJSON* wrapper_stage_object,
  const char* stage_key) {
  algomanager::algocatalog::AlgorithmPipelineWrapperStageSpec stage_spec{};
  if (!wrapper_stage_object || !stage_key || !*stage_key) {
    return stage_spec;
  }

  const cJSON* stage_item = cJSON_GetObjectItemCaseSensitive(wrapper_stage_object, stage_key);
  if (!stage_item) {
    return stage_spec;
  }

  stage_spec.declared = true;
  if (cJSON_IsString(stage_item) && stage_item->valuestring) {
    stage_spec.algorithm_name = json_utils::TrimText(stage_item->valuestring);
    return stage_spec;
  }
  if (!cJSON_IsObject(stage_item)) {
    return stage_spec;
  }

  stage_spec.algorithm_name = json_utils::TrimText(json_utils::GetStringField(stage_item, "algorithm_name"));
  if (stage_spec.algorithm_name.empty()) {
    stage_spec.algorithm_name = json_utils::TrimText(json_utils::GetStringField(stage_item, "algorithm"));
  }
  if (stage_spec.algorithm_name.empty()) {
    stage_spec.algorithm_name = json_utils::TrimText(json_utils::GetStringField(stage_item, "name"));
  }
  return stage_spec;
}

PackageDefaultSchema _LoadPackageDefaultSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  PackageDefaultSchema schema{};
  const fs::path package_default_json_path = algorithm::package_paths::ResolvePackageDefaultJsonPath(
    package_location.package_root,
    package_location.manifest_path);
  if (package_default_json_path.empty()) {
    schema.error_message = "Failed to resolve package default JSON file.";
    return schema;
  }

  std::error_code ec;
  if (!fs::exists(package_default_json_path, ec) || !fs::is_regular_file(package_default_json_path, ec)) {
    schema.valid = true;
    schema.has_default_file = false;
    return schema;
  }

  schema.has_default_file = true;

  const std::string json_text = json_utils::ReadTextFile(package_default_json_path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package default JSON file: " + package_default_json_path.generic_string();
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package default JSON file: " + package_default_json_path.generic_string();
    return schema;
  }

  const std::string algorithm_name = json_utils::GetStringField(root, "algorithm_name");
  const std::string expected_algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  if (algorithm_name.empty() || (!expected_algorithm_name.empty() && algorithm_name != expected_algorithm_name)) {
    schema.error_message =
      "Package default JSON algorithm_name mismatch: " + package_default_json_path.generic_string();
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* resource_bindings = cJSON_GetObjectItemCaseSensitive(root, "resource_bindings");
  if (resource_bindings) {
    if (!cJSON_IsArray(resource_bindings)) {
      schema.error_message =
        "Package default JSON resource_bindings must be an array: " + package_default_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    const int resource_count = cJSON_GetArraySize(resource_bindings);
    schema.resource_bindings.reserve(resource_count > 0 ? static_cast<size_t>(resource_count) : 0u);
    for (int i = 0; i < resource_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(resource_bindings, i);
      if (!item || !cJSON_IsObject(item)) {
        schema.error_message =
          "Package default JSON resource binding is invalid: " + package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      PackageDefaultResourceBinding binding{};
      binding.resource_name = json_utils::GetStringField(item, "resource_name");
      binding.resource_kind = json_utils::GetStringField(item, "resource_kind");
      binding.source_path = json_utils::GetStringField(item, "source_path");
      const cJSON* required_item = cJSON_GetObjectItemCaseSensitive(item, "required");
      if (required_item) {
        if (!cJSON_IsBool(required_item)) {
          schema.error_message =
            "Package default JSON resource binding required field must be boolean: " +
            package_default_json_path.generic_string();
          cJSON_Delete(root);
          return schema;
        }
        binding.required = cJSON_IsTrue(required_item);
      }
      if (binding.resource_name.empty() || binding.resource_kind.empty()) {
        schema.error_message =
          "Package default JSON resource binding is missing a name or kind: " +
          package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      schema.resource_bindings.push_back(std::move(binding));
    }
  }

  const cJSON* descriptor_values = cJSON_GetObjectItemCaseSensitive(root, "descriptor_values");
  if (descriptor_values) {
    if (!cJSON_IsArray(descriptor_values)) {
      schema.error_message =
        "Package default JSON descriptor_values must be an array: " + package_default_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    const int descriptor_count = cJSON_GetArraySize(descriptor_values);
    schema.descriptor_values.reserve(descriptor_count > 0 ? static_cast<size_t>(descriptor_count) : 0u);
    for (int i = 0; i < descriptor_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(descriptor_values, i);
      if (!item || !cJSON_IsObject(item)) {
        schema.error_message =
          "Package default JSON descriptor value is invalid: " + package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      PackageDefaultDescriptorValue value{};
      value.descriptor_name = json_utils::GetStringField(item, "descriptor_name");
      if (value.descriptor_name.empty()) {
        schema.error_message =
          "Package default JSON descriptor value is missing descriptor_name: " +
          package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      if (!cJSON_GetObjectItemCaseSensitive(item, "scalar_value") ||
          !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "scalar_value"))) {
        schema.error_message =
          "Package default JSON descriptor value is missing scalar_value: " +
          package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      value.scalar_value = cJSON_GetObjectItemCaseSensitive(item, "scalar_value")->valuedouble;
      schema.descriptor_values.push_back(std::move(value));
    }
  }

  cJSON_Delete(root);
  schema.valid = true;
  assert(schema.valid);
  for (const PackageDefaultResourceBinding& binding : schema.resource_bindings) {
    assert(!binding.resource_name.empty());
    assert(!binding.resource_kind.empty());
  }
  for (const PackageDefaultDescriptorValue& value : schema.descriptor_values) {
    assert(!value.descriptor_name.empty());
  }
  return schema;
}

}  // namespace

bool LoadAlgorithmInterventionFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algomanager::algoscheduler::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  return intervention_detail::LoadAlgorithmInterventionFromLocationImpl(
    package_location,
    out_intervention,
    out_error_message);
}

bool LoadAlgorithmVkExecutorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algomanager::algoscheduler::IAlgorithmVkExecutor>* out_vk_executor,
  std::string* out_error_message) {
  const bool loaded = vk_exec_detail::LoadAlgorithmVkExecutorFromLocationImpl(
    package_location,
    out_vk_executor,
    out_error_message);
  if (loaded) {
    return true;
  }
  if (out_error_message && out_error_message->rfind("Exec section is missing: ", 0) == 0) {
    out_vk_executor->reset();
    out_error_message->clear();
    return true;
  }
  return false;
}

bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPipelineWrapperSpec* out_wrapper_spec,
  std::string* out_error_message) {
  if (!out_wrapper_spec) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper spec output pointer is null.";
    }
    return false;
  }

  *out_wrapper_spec = {};
  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for pipeline wrapper spec.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* wrapper = cJSON_GetObjectItemCaseSensitive(root, "wrapper");
  if (!wrapper || !cJSON_IsObject(wrapper)) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  const cJSON* wrapper_stage = cJSON_GetObjectItemCaseSensitive(wrapper, "stage");
  if (!wrapper_stage || !cJSON_IsObject(wrapper_stage)) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_wrapper_spec->declared = true;
  out_wrapper_spec->stage_begin = _LoadWrapperStageSpec(wrapper_stage, "stageBegin");
  out_wrapper_spec->stage_end = _LoadWrapperStageSpec(wrapper_stage, "stageEnd");
  cJSON_Delete(root);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool LoadAlgorithmPackageReflectorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmReflector reflector{};
  std::string reflector_error_message;
  if (!_LoadPackageRuntimeReflector(package_location, &reflector, &reflector_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(reflector_error_message);
    }
    return false;
  }

  if (reflector.empty()) {
    out_reflector->reset();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  *out_reflector = std::make_shared<algorithm::AlgorithmReflector>(std::move(reflector));
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool LoadAlgorithmPackageTransferMapFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "AlgorithmRuntimeTransferMap output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmRuntimeTransferMap transfer_map{};
  bool has_transfer_map = false;
  std::string transfer_map_error_message;
  if (!_LoadPackageRuntimeTransferMap(
        package_location,
        &transfer_map,
        &has_transfer_map,
        &transfer_map_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(transfer_map_error_message);
    }
    return false;
  }

  if (!has_transfer_map) {
    out_transfer_map->reset();
    if (out_has_transfer_map) {
      *out_has_transfer_map = false;
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  *out_transfer_map = std::make_shared<algorithm::AlgorithmRuntimeTransferMap>(std::move(transfer_map));
  if (out_has_transfer_map) {
    *out_has_transfer_map = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algomanager::algoscheduler::AlgorithmObject* out_group,
  std::string* out_error_message,
  bool load_reflector) {
  const std::string probe_algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const bool emit_runner_probe = _ShouldEmitPipelineRunnerProbe(probe_algorithm_name);
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject output pointer is null.";
    }
    return false;
  }

  if (!package_location.valid) {
    if (out_error_message) {
      *out_error_message = "Algorithm package location is invalid.";
    }
    return false;
  }

  algomanager::algoscheduler::AlgorithmObject group{};
  group.algorithm_profile.algorithm_name = package_location.algorithm_name;
  group.algorithm_profile.container_manifest_name = package_location.manifest_name.empty()
    ? package_location.algorithm_name
    : package_location.manifest_name;
  group.runtime_package_root_path = package_location.runtime_package_root.generic_string();

  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "cache_loader_probe.log",
      "create_from_location.mount_metadata.begin algorithm=" + probe_algorithm_name);
  }
  AlgorithmObjectMountMetadataCacheEntry mount_metadata{};
  if (!_LoadAlgorithmObjectMountMetadata(package_location, &mount_metadata, out_error_message)) {
    return false;
  }
  if (!mount_metadata.valid || !mount_metadata.container_template) {
    if (out_error_message) {
      *out_error_message = "Algorithm mount metadata is invalid.";
    }
    return false;
  }
  group.shared_container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
  algorithm::CopyAlgorithmContainerSet(*mount_metadata.container_template, group.shared_container_set.get());
  group.tick_lifetime = mount_metadata.tick_lifetime;
  group.runtime_transfer_map = mount_metadata.has_runtime_transfer_map
    ? mount_metadata.runtime_transfer_map
    : std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>{};
  group.pipeline_external_write_reset_container_names =
    mount_metadata.pipeline_external_write_reset_container_names;
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "cache_loader_probe.log",
      "create_from_location.mount_metadata.end has_map=" +
        std::string(mount_metadata.has_runtime_transfer_map ? "true" : "false"));
  }

  std::shared_ptr<algomanager::algoscheduler::IAlgorithmIntervention> package_intervention{};
  if (!LoadAlgorithmInterventionFromLocation(
        package_location,
        &package_intervention,
        out_error_message)) {
    return false;
  }
  std::shared_ptr<algomanager::algoscheduler::IAlgorithmVkExecutor> package_vk_executor{};
  if (!LoadAlgorithmVkExecutorFromLocation(
        package_location,
        &package_vk_executor,
        out_error_message)) {
    return false;
  }
  group.intervention = package_intervention;
  group.vk_executor = package_vk_executor;
  group.cuda_symbol = true;
  const bool allow_reflector = load_reflector;
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "cache_loader_probe.log",
      "create_from_location.intervention_load.end present=" +
        std::string(group.intervention ? "true" : "false") +
        " vk_exec=" + std::string(group.vk_executor ? "true" : "false"));
  }

  auto _ApplyLaunchOnceReflectionPolicy = [&group]() {
    if (group.tick_lifetime == algomanager::AlgorithmTickLifetime::LaunchOnceThenHold &&
        group.algorithm_reflector &&
        group.algorithm_reflector->refresh_mode == algorithm::AlgorithmReflectionRefreshMode::EveryTick) {
      group.algorithm_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
    }
  };
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "cache_loader_probe.log",
      "create_from_location.plugin_check.begin has_plugin=" +
        std::string(package_location.has_plugin_module ? "true" : "false") +
        " path=" + package_location.plugin_module_path.string());
  }

  if (!package_location.has_plugin_module || package_location.plugin_module_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm package is missing its plugin module: " + probe_algorithm_name;
    }
    return false;
  }

  AlgorithmPluginComponents plugin_components{};
  std::string plugin_error_message;
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.begin");
  }
  if (package_location.has_plugin_module &&
      TryLoadAlgorithmPluginComponents(package_location, &plugin_components, &plugin_error_message)) {
    if (emit_runner_probe) {
      _AppendPipelineRunnerProbe(
        "cache_loader_probe.log",
        "create_from_location.plugin_load.end success jobs=" + std::string(plugin_components.jobs_symbol ? "true" : "false") +
           " vk=" + std::string(plugin_components.vk_symbol ? "true" : "false") +
          " cuda=" + std::string(plugin_components.cuda_symbol ? "true" : "false") +
          " reflector=" + std::string(plugin_components.reflector ? "true" : "false") +
          " intervention=" + std::string(plugin_components.intervention ? "true" : "false"));
    }
    group.jobs_symbol = plugin_components.jobs_symbol;
    group.vk_symbol = plugin_components.vk_symbol;
    group.cuda_symbol = plugin_components.cuda_symbol;
    group.compatibility_symbol = plugin_components.compatibility_symbol;
    group.jobs_executor = plugin_components.jobs_executor;
    group.vk_executor = plugin_components.vk_executor
      ? plugin_components.vk_executor
      : (plugin_components.vk_symbol ? package_vk_executor : std::shared_ptr<algomanager::algoscheduler::IAlgorithmVkExecutor>{});
    group.cuda_executor = plugin_components.cuda_executor;
    group.compatibility_executor = plugin_components.compatibility_executor;
    group.intervention = plugin_components.intervention
      ? package_intervention
      : std::shared_ptr<algomanager::algoscheduler::IAlgorithmIntervention>{};
    if (allow_reflector &&
        plugin_components.runtime_reflector &&
        !plugin_components.runtime_reflector->empty()) {
      group.algorithm_reflector = std::move(plugin_components.runtime_reflector);
    } else if (allow_reflector && plugin_components.reflector) {
      std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
      if (LoadAlgorithmPackageReflectorFromLocation(
            package_location,
            &runtime_reflector,
            nullptr)) {
        group.algorithm_reflector = std::move(runtime_reflector);
      }
    }
    _ApplyLaunchOnceReflectionPolicy();
    if (group.intervention) {
      if (_HasDeclaredContainerNameConflict(*group.shared_container_set, "__algorithm_intervention_signal")) {
        if (out_error_message) {
          *out_error_message =
            "Hidden intervention signal container conflicts with an existing container: " +
            std::string("__algorithm_intervention_signal") + " (" +
            package_location.package_file_path.generic_string() + ")";
        }
        return false;
      }
      _AppendHiddenInterventionSignalContainer(group.shared_container_set.get());
    }
    *out_group = std::move(group);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!plugin_error_message.empty()) {
    if (out_error_message) {
      *out_error_message = std::move(plugin_error_message);
    }
    return false;
  }
  if (out_error_message) {
    *out_error_message = "Algorithm plugin load returned no components: " + probe_algorithm_name;
  }
  return false;
}

bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algomanager::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algomanager::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) {
  if (!out_resource_bindings || !out_descriptor_values) {
    if (out_error_message) {
      *out_error_message = "Default binding output pointers are null.";
    }
    return false;
  }

  out_resource_bindings->clear();
  out_descriptor_values->clear();
  if (out_has_default_file) {
    *out_has_default_file = false;
  }

  if (!package_location.valid) {
    if (out_error_message) {
      *out_error_message = "Algorithm package location is invalid.";
    }
    return false;
  }

  const PackageDefaultSchema schema = _LoadPackageDefaultSchema(package_location);
  if (!schema.valid) {
    if (out_error_message) {
      *out_error_message = schema.error_message;
    }
    return false;
  }

  if (!schema.has_default_file) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  assert(schema.valid);
  for (const PackageDefaultResourceBinding& binding : schema.resource_bindings) {
    assert(!binding.resource_name.empty());
    assert(!binding.resource_kind.empty());
  }
  for (const PackageDefaultDescriptorValue& value : schema.descriptor_values) {
    assert(!value.descriptor_name.empty());
  }

  out_resource_bindings->reserve(schema.resource_bindings.size());
  for (const PackageDefaultResourceBinding& binding : schema.resource_bindings) {
    out_resource_bindings->push_back(algomanager::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
    });
  }
  out_descriptor_values->reserve(schema.descriptor_values.size());
  for (const PackageDefaultDescriptorValue& value : schema.descriptor_values) {
    out_descriptor_values->push_back(algomanager::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }

  if (out_has_default_file) {
    *out_has_default_file = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

namespace {

using CreateBundleFn = bool (*)(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algomanager::algocatalog::AlgorithmPluginBundle* out_bundle);

using CreateRuntimeReflectorFn = bool (*)(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);
void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

std::shared_ptr<void> _LoadModule(
  const std::filesystem::path& path,
  std::string* out_error_message) {
  const fs::path absolute_path = fs::absolute(path).lexically_normal();
  const std::wstring wide_path = absolute_path.wstring();
  HMODULE module = LoadLibraryExW(
    wide_path.c_str(),
    nullptr,
    LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!module) {
    if (out_error_message) {
      *out_error_message =
        "LoadLibraryExW failed for " + absolute_path.string() +
        " (GetLastError=" + std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
    }
    return {};
  }

  return std::shared_ptr<void>(
    module,
    [](void* handle) {
      if (handle) {
        FreeLibrary(static_cast<HMODULE>(handle));
      }
    });
}

template <typename T>
std::shared_ptr<T> _WrapPluginObject(
  T* object,
  void (*destroy_fn)(T*),
  const std::shared_ptr<void>& module_guard) {
  if (!object || !destroy_fn) {
    return {};
  }
  return std::shared_ptr<T>(
    object,
    [module_guard, destroy_fn](T* ptr) {
      (void)module_guard;
      if (ptr) {
        destroy_fn(ptr);
      }
    });
}

}  // namespace

bool TryLoadAlgorithmPluginComponents(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPluginComponents* out_components,
  std::string* out_error_message) {
  if (!out_components) {
    _SetErrorMessage(out_error_message, "AlgorithmPluginComponents output pointer is null.");
    return false;
  }

  *out_components = {};
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }

  const std::filesystem::path plugin_path = package_location.plugin_module_path;
  if (plugin_path.empty()) {
    return false;
  }

  std::string load_error_message{};
  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    _AppendPipelineRunnerProbe(
      "cache_loader_probe.log",
      "create_from_location.plugin_load.module.begin path=" +
        std::filesystem::absolute(plugin_path).lexically_normal().string());
  }
  const std::shared_ptr<void> module_guard = _LoadModule(plugin_path, &load_error_message);
  if (!module_guard) {
    if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
          ? package_location.manifest_name
          : package_location.algorithm_name)) {
      _AppendPipelineRunnerProbe(
        "cache_loader_probe.log",
        "create_from_location.plugin_load.module.end failed error=" + load_error_message);
    }
    _SetErrorMessage(
      out_error_message,
      load_error_message.empty()
        ? "Failed to load algorithm plugin module: " +
          std::filesystem::absolute(plugin_path).lexically_normal().string()
        : load_error_message);
    return false;
  }
  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.module.end success");
  }

  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.proc.begin");
  }
  const auto create_bundle_fn = reinterpret_cast<CreateBundleFn>(
    GetProcAddress(static_cast<HMODULE>(module_guard.get()), "AlgorithmPlugin_CreateBundle"));
  if (!create_bundle_fn) {
    if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
          ? package_location.manifest_name
          : package_location.algorithm_name)) {
      _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.proc.end missing");
    }
    _SetErrorMessage(out_error_message, "Algorithm plugin is missing AlgorithmPlugin_CreateBundle: " + plugin_path.string());
    return false;
  }
  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.proc.end success");
  }

  algomanager::algocatalog::AlgorithmPluginRequest request{};
  const std::filesystem::path plugin_request_package_root =
    package_location.source_package_root.empty()
      ? package_location.package_root
      : package_location.source_package_root;
  const std::string algorithm_library_root = plugin_request_package_root.has_parent_path()
    ? plugin_request_package_root.parent_path().generic_string()
    : plugin_request_package_root.generic_string();
  const std::string algorithm_folder = plugin_request_package_root.filename().generic_string();
  request.algorithm_name = package_location.algorithm_name.c_str();
  request.algorithm_library_root = algorithm_library_root.c_str();
  request.algorithm_folder = algorithm_folder.c_str();

  algomanager::algocatalog::AlgorithmPluginBundle bundle{};
  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_bundle.begin");
  }
  if (!create_bundle_fn(&request, &bundle)) {
    if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
          ? package_location.manifest_name
          : package_location.algorithm_name)) {
      _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_bundle.end failed");
    }
    _SetErrorMessage(out_error_message, "Algorithm plugin rejected bundle creation: " + plugin_path.string());
    return false;
  }
  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_bundle.end success");
  }

  if (request.api_version != algomanager::algocatalog::kAlgorithmPluginApiVersion) {
    _SetErrorMessage(
      out_error_message,
      "Algorithm plugin request ABI version mismatch for: " + plugin_path.string());
    return false;
  }
  if (bundle.api_version != algomanager::algocatalog::kAlgorithmPluginApiVersion) {
    _SetErrorMessage(
      out_error_message,
      "Algorithm plugin bundle ABI version mismatch for: " + plugin_path.string());
    return false;
  }

  out_components->jobs_symbol = bundle.jobs_symbol;
  out_components->vk_symbol = bundle.vk_symbol;
  out_components->cuda_symbol = bundle.cuda_symbol;
  out_components->compatibility_symbol = bundle.compatibility_symbol;
  out_components->reflector = bundle.reflector;
  out_components->intervention = bundle.intervention;

  if (bundle.vk_executor && bundle.destroy_vk_executor) {
    out_components->vk_executor = _WrapPluginObject(
      bundle.vk_executor,
      bundle.destroy_vk_executor,
      module_guard);
  }
  if (bundle.cuda_executor && bundle.destroy_cuda_executor) {
    out_components->cuda_executor = _WrapPluginObject(
      bundle.cuda_executor,
      bundle.destroy_cuda_executor,
      module_guard);
  }
  if (bundle.compatibility_executor && bundle.destroy_compatibility_executor) {
    out_components->compatibility_executor = _WrapPluginObject(
      bundle.compatibility_executor,
      bundle.destroy_compatibility_executor,
      module_guard);
  }
  if (bundle.jobs_executor && bundle.destroy_jobs_executor) {
    out_components->jobs_executor = _WrapPluginObject(
      bundle.jobs_executor,
      bundle.destroy_jobs_executor,
      module_guard);
  }

  if (_ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name) &&
      bundle.reflector) {
    _AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.reflector.skipped");
  }
  _SetErrorMessage(out_error_message, {});
  return true;
}

}  // namespace catalog

namespace bridge { namespace runtime_bridge_support {

std::string ResolveAlgorithmVkShaderPath(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message) {
  if (shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "VK shader path must not be empty.";
    }
    return {};
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return path.string();
  }

  std::filesystem::path runtime_package_root(object.runtime_package_root_path);
  if (runtime_package_root.empty()) {
    ::algorithm::AlgorithmPackageLocation package_location{};
    std::string resolve_error_message;
    if (!::algorithm::TryResolveAlgorithmPackageLocation(
          object.algorithm_profile.algorithm_name,
          &package_location,
          &resolve_error_message)) {
      if (out_error_message) {
        *out_error_message = resolve_error_message.empty()
          ? ("Failed to resolve algorithm package location for '" + object.algorithm_profile.algorithm_name + "'.")
          : std::move(resolve_error_message);
      }
      return {};
    }
    runtime_package_root = package_location.runtime_package_root;
  }
  if (runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Algorithm runtime package root is empty for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return {};
  }

  const std::filesystem::path resolved_path = (runtime_package_root / path).lexically_normal();
  if (resolved_path.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Failed to resolve VK shader path for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return {};
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return resolved_path.string();
}

bool TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message) {
  if (!container_set || !out_stage_job) {
    if (out_error_message) {
      *out_error_message = "VK stage sub-job output pointer is null.";
    }
    return false;
  }
  if (phase_spec.shader.vertex_shader_path.empty() || phase_spec.shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "VK phase is missing shader paths.";
    }
    return false;
  }
  if (phase_spec.used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "VK phase does not bind any containers.";
    }
    return false;
  }

  out_stage_job->debug_name = object.algorithm_profile.algorithm_name + "::" + phase_spec.stage_name;
  out_stage_job->stage_name = phase_spec.stage_name;
  out_stage_job->vertex_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    phase_spec.shader.vertex_shader_path,
    out_error_message);
  if (out_stage_job->vertex_shader_path.empty()) {
    return false;
  }
  out_stage_job->fragment_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    phase_spec.shader.fragment_shader_path,
    out_error_message);
  if (out_stage_job->fragment_shader_path.empty()) {
    return false;
  }

  out_stage_job->buffer_bindings.reserve(phase_spec.used_algorithm_containers.size());
  for (const ::algomanager::algoscheduler::AlgorithmPhaseContainerBinding& binding : phase_spec.used_algorithm_containers) {
    ::algorithm::AlgorithmContainer* container =
      ::algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK phase is missing container '" + binding.container_name + "'.")
          : ("VK optional container '" + binding.container_name +
              "' is not supported because runtime VK bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK phase container '" + binding.container_name + "' has no data.")
          : ("VK optional container '" + binding.container_name +
              "' has no data, and sparse VK bindings are not supported.");
      }
      return false;
    }

    runtimesys::RuntimeVkBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = binding.container_kind == "array";
    binding_view.draw_indirect = binding.container_kind == "draw_indirect";
    binding_view.required = binding.required;
    out_stage_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool TryBuildAlgorithmVkExecStageSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message) {
  if (!container_set || !out_stage_job) {
    if (out_error_message) {
      *out_error_message = "VK exec stage sub-job output pointer is null.";
    }
    return false;
  }
  if (vk_exec_spec.shader.vertex_shader_path.empty() || vk_exec_spec.shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "VK exec stage is missing shader paths.";
    }
    return false;
  }
  if (vk_exec_spec.used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "VK exec stage does not bind any containers.";
    }
    return false;
  }

  out_stage_job->debug_name = object.algorithm_profile.algorithm_name + "::" + vk_exec_spec.stage_name;
  out_stage_job->stage_name = vk_exec_spec.stage_name;
  out_stage_job->vertex_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    vk_exec_spec.shader.vertex_shader_path,
    out_error_message);
  if (out_stage_job->vertex_shader_path.empty()) {
    return false;
  }
  out_stage_job->fragment_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    vk_exec_spec.shader.fragment_shader_path,
    out_error_message);
  if (out_stage_job->fragment_shader_path.empty()) {
    return false;
  }

  out_stage_job->buffer_bindings.reserve(vk_exec_spec.used_algorithm_containers.size());
  for (const ::algomanager::algoscheduler::AlgorithmVkExecContainerBinding& binding : vk_exec_spec.used_algorithm_containers) {
    ::algorithm::AlgorithmContainer* container =
      ::algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK exec stage is missing container '" + binding.container_name + "'.")
          : ("VK exec optional container '" + binding.container_name +
              "' is not supported because runtime VK bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK exec stage container '" + binding.container_name + "' has no data.")
          : ("VK exec optional container '" + binding.container_name +
              "' has no data, and sparse VK bindings are not supported.");
      }
      return false;
    }

    runtimesys::RuntimeVkBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = binding.container_kind == "array";
    binding_view.draw_indirect = binding.container_kind == "draw_indirect";
    binding_view.required = binding.required;
    out_stage_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace runtime_bridge_support
}  // namespace bridge

}  // namespace algomanager

#include "algomanager/algorithm_manager.cpp"

