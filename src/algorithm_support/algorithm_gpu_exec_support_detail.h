#pragma once

#include "algorithm_support/algorithm_json_utils.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"

#include "cJSON.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm_support::gpu_exec_detail {

namespace fs = std::filesystem;

struct GpuExecSchema {
  agent::AlgorithmGpuExecSpec spec{};
  bool valid{false};
  std::string error_message;
};

inline std::string AlgorithmNameFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location) {
  if (!package_location.algorithm_name.empty()) {
    return package_location.algorithm_name;
  }
  return package_location.manifest_name;
}

inline void LoadGpuExecBindings(
  const cJSON* used_containers,
  std::vector<agent::AlgorithmGpuExecContainerBinding>* out_bindings,
  const std::string& package_path,
  std::string* out_error_message) {
  if (!out_bindings) {
    if (out_error_message) {
      *out_error_message = "GPU exec bindings output pointer is null.";
    }
    return;
  }
  if (!used_containers || !cJSON_IsObject(used_containers)) {
    return;
  }

  const auto append_binding_group = [&](const cJSON* items, const char* default_kind, uint32_t default_tuple_width) {
    if (!items || !cJSON_IsArray(items)) {
      return true;
    }

    const int container_count = cJSON_GetArraySize(items);
    out_bindings->reserve(
      out_bindings->size() + (container_count > 0 ? static_cast<size_t>(container_count) : 0u));
    for (int i = 0; i < container_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(items, i);
      if (!item) {
        continue;
      }

      agent::AlgorithmGpuExecContainerBinding binding{};
      if (cJSON_IsString(item) && item->valuestring) {
        binding.container_name = item->valuestring;
        binding.container_kind = default_kind ? default_kind : "";
        binding.tuple_width = default_tuple_width;
        binding.required = true;
      } else if (cJSON_IsObject(item)) {
        binding.container_name = json_utils::GetStringField(item, "name");
        if (binding.container_name.empty()) {
          binding.container_name = json_utils::GetStringField(item, "container");
        }
        binding.container_kind = json_utils::GetStringField(item, "kind");
        if (binding.container_kind.empty()) {
          binding.container_kind = default_kind ? default_kind : "";
        }
        binding.tuple_width = json_utils::GetUintField(item, "tuple_width", default_tuple_width);
        if (binding.tuple_width == 0u) {
          binding.tuple_width = default_tuple_width;
        }
        binding.required = json_utils::GetBoolField(item, "required", true);
      }

      if (binding.container_name.empty()) {
        if (out_error_message) {
          *out_error_message = "Invalid GPU exec container binding in package JSON file: " + package_path;
        }
        return false;
      }
      out_bindings->push_back(std::move(binding));
    }
    return true;
  };

  const cJSON* arrays = cJSON_GetObjectItemCaseSensitive(used_containers, "arrays");
  if (!append_binding_group(arrays, "array", 1u)) {
    return;
  }

  const cJSON* variables = cJSON_GetObjectItemCaseSensitive(used_containers, "variables");
  if (!append_binding_group(variables, "variable", 1u)) {
    return;
  }
}

inline GpuExecSchema LoadGpuExecSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  GpuExecSchema schema{};
  const std::string algorithm_name = AlgorithmNameFromLocation(package_location);
  const fs::path path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (path.empty()) {
    schema.error_message = "Failed to resolve package JSON file.";
    return schema;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + path.string();
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + path.string();
    return schema;
  }

  const cJSON* exec = cJSON_GetObjectItemCaseSensitive(root, "exec");
  if (!exec) {
    cJSON_Delete(root);
    return schema;
  }
  if (!cJSON_IsObject(exec)) {
    schema.error_message = "Exec section must be an object: " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  schema.spec.stage_name = json_utils::GetStringField(exec, "stage_name");
  if (schema.spec.stage_name.empty()) {
    schema.spec.stage_name = "exec";
  }

  const cJSON* functions = cJSON_GetObjectItemCaseSensitive(exec, "functions");
  if (functions) {
    if (!cJSON_IsArray(functions)) {
      schema.error_message = "Exec functions must be an array: " + path.string();
      cJSON_Delete(root);
      return schema;
    }
    const int function_count = cJSON_GetArraySize(functions);
    schema.spec.functions.reserve(function_count > 0 ? static_cast<size_t>(function_count) : 0u);
    for (int i = 0; i < function_count; ++i) {
      const cJSON* function_item = cJSON_GetArrayItem(functions, i);
      if (function_item && cJSON_IsString(function_item) && function_item->valuestring) {
        schema.spec.functions.emplace_back(function_item->valuestring);
      }
    }
  }

  std::string binding_error_message;
  LoadGpuExecBindings(
    cJSON_GetObjectItemCaseSensitive(exec, "used_algorithm_containers"),
    &schema.spec.used_algorithm_containers,
    path.string(),
    &binding_error_message);
  if (!binding_error_message.empty()) {
    schema.error_message = std::move(binding_error_message);
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* shader = cJSON_GetObjectItemCaseSensitive(exec, "shader");
  if (!shader || !cJSON_IsObject(shader)) {
    schema.error_message = "Exec shader section is missing or invalid: " + path.string();
    cJSON_Delete(root);
    return schema;
  }
  schema.spec.shader.vertex_shader_path = json_utils::GetStringField(shader, "vertex");
  schema.spec.shader.fragment_shader_path = json_utils::GetStringField(shader, "fragment");
  schema.spec.shader.pipeline_kind = json_utils::GetStringField(shader, "pipeline");

  if (schema.spec.used_algorithm_containers.empty()) {
    schema.error_message = "Exec stage must bind at least one algorithm container: " + path.string();
    cJSON_Delete(root);
    return schema;
  }
  if (schema.spec.shader.vertex_shader_path.empty() ||
      schema.spec.shader.fragment_shader_path.empty()) {
    schema.error_message = "Exec stage is missing shader paths: " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  cJSON_Delete(root);
  schema.valid = true;
  return schema;
}

class JsonAlgorithmGpuExecutor final : public agent::IAlgorithmGpuExecutor {
 public:
  explicit JsonAlgorithmGpuExecutor(agent::AlgorithmGpuExecSpec spec)
    : spec_(std::move(spec)) {}

  bool GetGpuExecSpec(agent::AlgorithmGpuExecSpec* out_spec) const override {
    if (!out_spec) {
      return false;
    }
    *out_spec = spec_;
    return true;
  }

 private:
  agent::AlgorithmGpuExecSpec spec_{};
};

inline bool LoadAlgorithmGpuExecutorFromLocationImpl(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agent::IAlgorithmGpuExecutor>* out_gpu_executor,
  std::string* out_error_message) {
  if (!out_gpu_executor) {
    if (out_error_message) {
      *out_error_message = "GPU executor output pointer is null.";
    }
    return false;
  }

  const GpuExecSchema schema = LoadGpuExecSchema(package_location);
  if (!schema.valid) {
    if (schema.error_message.empty()) {
      out_gpu_executor->reset();
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
    if (out_error_message) {
      *out_error_message = schema.error_message;
    }
    return false;
  }

  *out_gpu_executor = std::make_shared<JsonAlgorithmGpuExecutor>(schema.spec);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_support::gpu_exec_detail
