#pragma once

#include "algorithm_support/algorithm_intervention_support.h"
#include "algorithm_support/algorithm_library_paths.h"
#include "algorithm_support/algorithm_json_utils.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"

#include "cJSON.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm_support::intervention_detail {

namespace fs = std::filesystem;

inline bool ShouldEmitPipelineRunnerProbe(const std::string& algorithm_name) {
  return algorithm_name.find("v2a0_pipeline_square_vertex_demo") != std::string::npos ||
    algorithm_name.find("runner_mount") != std::string::npos;
}

inline void AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line) {
  const fs::path path = algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() /
    file_name;
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

struct InterventionSchema {
  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  bool valid{false};
  std::string error_message;
};

inline std::string AlgorithmNameFromLocation(const algorithm::AlgorithmPackageLocation& package_location) {
  if (!package_location.algorithm_name.empty()) {
    return package_location.algorithm_name;
  }
  return package_location.manifest_name;
}

inline bool ParseInterventionStageKind(
  const std::string& stage_name,
  const std::string& stage_kind_text,
  agent::AlgorithmInterventionStageKind* out_stage_kind) {
  if (!out_stage_kind) {
    return false;
  }

  const std::string kind = !stage_kind_text.empty() ? stage_kind_text : stage_name;
  if (kind == "pretick" || kind == "preTick") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::Pretick;
    return true;
  }
  if (kind == "exec") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::Exec;
    return true;
  }
  if (kind == "aftertick" || kind == "afterTick" || kind == "postExecution") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::AfterTick;
    return true;
  }
  if (kind == "renderresult" || kind == "renderResult" || kind == "resultRender") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::RenderResult;
    return true;
  }
  if (kind == "reflect") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::Reflect;
    return true;
  }

  *out_stage_kind = agent::AlgorithmInterventionStageKind::Custom;
  return true;
}

inline InterventionSchema LoadInterventionSchema(const algorithm::AlgorithmPackageLocation& package_location) {
  InterventionSchema schema{};
  const std::string algorithm_name = AlgorithmNameFromLocation(package_location);
  const bool emit_runner_probe = ShouldEmitPipelineRunnerProbe(algorithm_name);
  const fs::path path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (emit_runner_probe) {
    AppendPipelineRunnerProbe(
      "intervention_loader_probe.log",
      "load.begin algorithm=" + algorithm_name + " path=" + path.string());
  }
  if (path.empty()) {
    schema.error_message = "Failed to resolve package JSON file.";
    return schema;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + path.string();
    return schema;
  }
  if (emit_runner_probe) {
    AppendPipelineRunnerProbe(
      "intervention_loader_probe.log",
      "load.read.end size=" + std::to_string(json_text.size()));
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + path.string();
    return schema;
  }
  if (emit_runner_probe) {
    AppendPipelineRunnerProbe("intervention_loader_probe.log", "load.parse.end");
  }

  const cJSON* intervention = cJSON_GetObjectItemCaseSensitive(root, "intervention");
  if (!intervention || !cJSON_IsObject(intervention)) {
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* singular_stage = cJSON_GetObjectItemCaseSensitive(intervention, "stage");
  const cJSON* plural_stages = cJSON_GetObjectItemCaseSensitive(intervention, "stages");
  if (singular_stage && plural_stages) {
    schema.error_message =
      "Intervention section must not declare both 'stage' and 'stages': " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* stages = singular_stage ? singular_stage : plural_stages;
  if (!stages) {
    cJSON_Delete(root);
    return schema;
  }
  if (emit_runner_probe) {
    AppendPipelineRunnerProbe(
      "intervention_loader_probe.log",
      "load.stage_section.end source=" + std::string(singular_stage ? "stage" : "stages"));
  }
  if (!cJSON_IsObject(stages)) {
    schema.error_message = "Intervention stage section is invalid: " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  for (const cJSON* stage_item = stages->child; stage_item; stage_item = stage_item->next) {
    if (emit_runner_probe) {
      AppendPipelineRunnerProbe(
        "intervention_loader_probe.log",
        "load.stage_item.begin key=" + std::string(stage_item && stage_item->string ? stage_item->string : "<null>"));
    }
    if (!stage_item || !cJSON_IsObject(stage_item) || !stage_item->string) {
      continue;
    }

    const std::string stage_key = stage_item->string;
    agent::AlgorithmInterventionStageSpec stage_spec{};
    stage_spec.stage_name = json_utils::GetStringField(stage_item, "stage_name");
    if (stage_spec.stage_name.empty()) {
      stage_spec.stage_name = stage_key;
    }

    const std::string stage_kind_text = json_utils::GetStringField(stage_item, "stage_kind");
    if (!ParseInterventionStageKind(stage_spec.stage_name, stage_kind_text, &stage_spec.stage_kind)) {
      schema.error_message = "Invalid stage kind in package JSON file: " + path.string();
      cJSON_Delete(root);
      return schema;
    }

    const cJSON* used_containers = cJSON_GetObjectItemCaseSensitive(stage_item, "used_algorithm_containers");
    if (used_containers && cJSON_IsObject(used_containers)) {
      const cJSON* arrays = cJSON_GetObjectItemCaseSensitive(used_containers, "arrays");
      if (arrays && cJSON_IsArray(arrays)) {
        const int container_count = cJSON_GetArraySize(arrays);
        stage_spec.used_algorithm_containers.reserve(
          stage_spec.used_algorithm_containers.size() +
          (container_count > 0 ? static_cast<size_t>(container_count) : 0u));
        for (int i = 0; i < container_count; ++i) {
          const cJSON* item = cJSON_GetArrayItem(arrays, i);
          if (!item) {
            continue;
          }

          agent::AlgorithmInterventionContainerBinding binding{};
          if (cJSON_IsString(item) && item->valuestring) {
            binding.container_name = item->valuestring;
            binding.container_kind = "array";
            binding.tuple_width = 3u;
            binding.required = true;
          } else if (cJSON_IsObject(item)) {
            binding.container_name = json_utils::GetStringField(item, "name");
            if (binding.container_name.empty()) {
              binding.container_name = json_utils::GetStringField(item, "container");
            }
            binding.container_kind = json_utils::GetStringField(item, "kind");
            if (binding.container_kind.empty()) {
              binding.container_kind = "array";
            }
            binding.tuple_width = json_utils::GetUintField(item, "tuple_width", 3u);
            if (binding.tuple_width == 0u) {
              binding.tuple_width = 3u;
            }
            binding.required = json_utils::GetBoolField(item, "required", true);
          }

          if (binding.container_name.empty()) {
            schema.error_message = "Invalid array container binding in package JSON file: " + path.string();
            cJSON_Delete(root);
            return schema;
          }
          stage_spec.used_algorithm_containers.push_back(std::move(binding));
        }
      }
    }

    const cJSON* functions = cJSON_GetObjectItemCaseSensitive(stage_item, "functions");
    if (functions && cJSON_IsArray(functions)) {
      const int function_count = cJSON_GetArraySize(functions);
      stage_spec.functions.reserve(function_count > 0 ? static_cast<size_t>(function_count) : 0u);
      for (int i = 0; i < function_count; ++i) {
        const cJSON* function_item = cJSON_GetArrayItem(functions, i);
        if (function_item && cJSON_IsString(function_item) && function_item->valuestring) {
          stage_spec.functions.emplace_back(function_item->valuestring);
        }
      }
    }

    const cJSON* shader = cJSON_GetObjectItemCaseSensitive(stage_item, "shader");
    if (shader && cJSON_IsObject(shader)) {
      stage_spec.shader.vertex_shader_path = json_utils::GetStringField(shader, "vertex");
      stage_spec.shader.fragment_shader_path = json_utils::GetStringField(shader, "fragment");
      stage_spec.shader.pipeline_kind = json_utils::GetStringField(shader, "pipeline");
    }

    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      if (stage_spec.used_algorithm_containers.empty()) {
        schema.error_message =
          "Result-render stage in package JSON file must bind at least one array container: " + path.string();
        cJSON_Delete(root);
        return schema;
      }
      if (stage_spec.shader.vertex_shader_path.empty() || stage_spec.shader.fragment_shader_path.empty()) {
        schema.error_message =
          "Result-render stage in package JSON file is missing shader paths: " + path.string();
        cJSON_Delete(root);
        return schema;
      }
    }

    schema.stage_specs.push_back(std::move(stage_spec));
    if (emit_runner_probe) {
      AppendPipelineRunnerProbe(
        "intervention_loader_probe.log",
        "load.stage_item.end key=" + stage_key);
    }
  }

  cJSON_Delete(root);
  schema.valid = !schema.stage_specs.empty();
  if (emit_runner_probe) {
    AppendPipelineRunnerProbe(
      "intervention_loader_probe.log",
      "load.end valid=" + std::string(schema.valid ? "true" : "false") +
      " count=" + std::to_string(schema.stage_specs.size()));
  }
  return schema;
}

class JsonAlgorithmIntervention final : public agent::IAlgorithmIntervention {
 public:
  explicit JsonAlgorithmIntervention(InterventionSchema schema)
    : schema_(std::move(schema)) {}

  bool SupportsIntervention() const override {
    return schema_.valid && !schema_.stage_specs.empty();
  }

  void FillAgentToAlgorithmSignal(
    const agent::AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const override {
    if (!out_signal) {
      return;
    }

    *out_signal = {};
    out_signal->needs_intervention = context.intervention_request && context.intervention_request->enabled;
    out_signal->control_bits = context.intervention_request ? context.intervention_request->control_bits : 0u;
  }

  bool GetInterventionStageSpecs(
    std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs) const override {
    if (!out_stage_specs) {
      return false;
    }
    *out_stage_specs = schema_.stage_specs;
    return schema_.valid && !schema_.stage_specs.empty();
  }

 private:
  InterventionSchema schema_{};
};

inline bool LoadAlgorithmInterventionFromLocationImpl(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  if (!out_intervention) {
    if (out_error_message) {
      *out_error_message = "Intervention output pointer is null.";
    }
    return false;
  }

  const InterventionSchema schema = LoadInterventionSchema(package_location);
  if (!schema.valid) {
    if (schema.error_message.empty()) {
      out_intervention->reset();
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
    if (out_error_message) {
      *out_error_message = schema.error_message.empty()
        ? "Failed to load intervention schema from package location."
        : schema.error_message;
    }
    return false;
  }

  *out_intervention = std::make_shared<JsonAlgorithmIntervention>(schema);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_support::intervention_detail
