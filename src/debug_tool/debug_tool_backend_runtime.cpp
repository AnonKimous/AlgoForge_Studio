#include "debug_tool_backend_runtime.h"

#include "algorithm_management/algorithm_manager.h"
#include "algorithm_support/algorithm_library_paths.h"
#include "common_data/kernel_cfg.h"
#include "cJSON.h"

#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace debug_tool_backend {

namespace {

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

std::string _AlgorithmCatalogPath() {
  return (algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot() / "algorithm_catalog.json").string();
}

std::string _ReadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetJsonStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

std::string _ProjectRootPath() {
  const std::filesystem::path root = algorithm::library_paths::ResolveProjectRootFromAlgorithmLibraryRoot(
    algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot());
  if (!root.empty()) {
    return root.string();
  }
  return ".";
}

std::string _MakeCIdentifier(const std::string& text) {
  std::string result;
  result.reserve(text.size() + 1u);
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    const bool is_identifier_char =
      (ch >= 'a' && ch <= 'z') ||
      (ch >= 'A' && ch <= 'Z') ||
      (ch >= '0' && ch <= '9') ||
      ch == '_';
    if (i == 0u && (ch >= '0' && ch <= '9')) {
      result.push_back('_');
    }
    result.push_back(is_identifier_char ? static_cast<char>(ch) : '_');
  }
  return result;
}

bool _ShouldEmitPipelineRunnerProbe(const std::string& pipeline_name) {
  return pipeline_name.find("::runner_mount") != std::string::npos;
}

void _AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line) {
  const std::filesystem::path path =
    std::filesystem::path("D:/gptsandbox/artifacts/pipeline_runner") / file_name;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

std::string _HotReloadBuildCommand(const std::string& algorithm_name) {
  const std::string root = _ProjectRootPath();
  const std::string script_path = (std::filesystem::path(root) / "build_algorithm.bat").string();
  const std::string target_name = _MakeCIdentifier(algorithm_name);
  return "\"" + script_path + "\" \"" + target_name + "\"";
}

std::string _ResolveAlgorithmShaderPath(
  const std::string& algorithm_name,
  const std::string& shader_path) {
  if (shader_path.empty()) {
    return {};
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    return path.string();
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string error_message;
  const bool resolved = algorithm_management::TryResolveAlgorithmPackageLocation(
    algorithm_name,
    &package_location,
    &error_message);
  DEBUG_TOOL_ASSERT(resolved, "Failed to resolve algorithm package location for shader path.");
  if (!resolved || package_location.runtime_package_root.empty()) {
    return {};
  }

  return (package_location.runtime_package_root / path).lexically_normal().string();
}

std::string _ResolveShaderBinaryPath(const std::string& shader_path) {
  if (shader_path.empty()) {
    return {};
  }

  const std::filesystem::path path(shader_path);
  const std::string path_text = path.string();
  if (path_text.size() >= 4u && path_text.compare(path_text.size() - 4u, 4u, ".spv") == 0) {
    return path_text;
  }
  return path_text + ".spv";
}

bool _IsReadableNonEmptyFile(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  const std::filesystem::path file_path(path);
  if (!std::filesystem::exists(file_path, ec) || ec) {
    return false;
  }
  if (!std::filesystem::is_regular_file(file_path, ec) || ec) {
    return false;
  }
  const uintmax_t file_size = std::filesystem::file_size(file_path, ec);
  return !ec && file_size > 0u;
}

const agent::AlgorithmReflectionValue* _FindReflectionValue(
  const agent::AlgorithmReflectionSnapshot& snapshot,
  const std::string& container_name) {
  for (const agent::AlgorithmReflectionValue& value : snapshot.variables) {
    if (value.container_name == container_name) {
      return &value;
    }
  }
  for (const agent::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    if (value.container_name == container_name) {
      return &value;
    }
  }
  return nullptr;
}

std::string _AlgorithmContainerStorageKindToString(AlgorithmContainerStorageKind storage_kind) {
  switch (storage_kind) {
    case AlgorithmContainerStorageKind::Array: return "array";
    case AlgorithmContainerStorageKind::TemporaryRegister: return "temporary_register";
    case AlgorithmContainerStorageKind::TemporaryCache: return "temporary_cache";
  }
  return "unknown";
}

debug_tool::AlgorithmAssemblyState _ToDebugToolAlgorithmAssemblyState(agent::AlgorithmAssemblyState state) {
  switch (state) {
    case agent::AlgorithmAssemblyState::Pending: return debug_tool::AlgorithmAssemblyState::Pending;
    case agent::AlgorithmAssemblyState::Assembling: return debug_tool::AlgorithmAssemblyState::Assembling;
    case agent::AlgorithmAssemblyState::Ready: return debug_tool::AlgorithmAssemblyState::Ready;
    case agent::AlgorithmAssemblyState::Failed: return debug_tool::AlgorithmAssemblyState::Failed;
  }
  return debug_tool::AlgorithmAssemblyState::Failed;
}

debug_tool::AlgorithmReflectionValue _ToDebugToolReflectionValue(const agent::AlgorithmReflectionValue& value) {
  debug_tool::AlgorithmReflectionValue out_value{};
  out_value.reflection_object_name = value.reflection_object_name;
  out_value.container_name = value.container_name;
  out_value.filter_name = value.filter_name;
  out_value.storage_kind = _AlgorithmContainerStorageKindToString(value.storage_kind);
  out_value.bytes = value.bytes;
  return out_value;
}

debug_tool::AlgorithmReflectionSnapshot _ToDebugToolReflectionSnapshot(
  const agent::AlgorithmReflectionSnapshot& snapshot) {
  debug_tool::AlgorithmReflectionSnapshot out_snapshot{};
  out_snapshot.algorithm_name = snapshot.algorithm_name;
  out_snapshot.valid = snapshot.valid;
  out_snapshot.variables.reserve(snapshot.variables.size());
  for (const agent::AlgorithmReflectionValue& value : snapshot.variables) {
    out_snapshot.variables.push_back(_ToDebugToolReflectionValue(value));
  }
  out_snapshot.variable_arrays.reserve(snapshot.variable_arrays.size());
  for (const agent::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    out_snapshot.variable_arrays.push_back(_ToDebugToolReflectionValue(value));
  }
  return out_snapshot;
}

void _AppendBridgeReflectionValue(
  const algorithm::AlgorithmContainer& container,
  const std::string& filter_name,
  agent::AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return;
  }

  agent::AlgorithmReflectionValue value{};
  value.reflection_object_name = container.name;
  value.container_name = container.name;
  value.filter_name = filter_name;
  value.storage_kind = container.storage_kind;
  value.bytes.assign(container.bytes.begin(), container.bytes.end());

  if (container.storage_kind == algorithm::AlgorithmContainerStorageKind::Array) {
    out_snapshot->variable_arrays.push_back(std::move(value));
    return;
  }
  out_snapshot->variables.push_back(std::move(value));
}

agent::AlgorithmReflectionSnapshot _BuildBridgeReflectionSnapshot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& filter_name) {
  agent::AlgorithmReflectionSnapshot snapshot{};
  snapshot.algorithm_name = container_set.algorithm_name;
  snapshot.variables.reserve(
    container_set.temporary_registers.size() +
    container_set.temporary_caches.size() +
    container_set.hidden_containers.size());
  snapshot.variable_arrays.reserve(container_set.arrays.size());

  for (const algorithm::AlgorithmContainer& container : container_set.arrays) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }
  for (const algorithm::AlgorithmContainer& container : container_set.temporary_registers) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }
  for (const algorithm::AlgorithmContainer& container : container_set.temporary_caches) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }
  for (const algorithm::AlgorithmContainer& container : container_set.hidden_containers) {
    _AppendBridgeReflectionValue(container, filter_name, &snapshot);
  }

  snapshot.valid = !snapshot.variables.empty() || !snapshot.variable_arrays.empty();
  return snapshot;
}

debug_tool::PipelineStageBridgeDebugSummary _ToDebugToolPipelineStageBridgeDebugSummary(
  const agent::PipelineStageBridgeDebugSet& debug_set) {
  debug_tool::PipelineStageBridgeDebugSummary out_summary{};
  if (!debug_set.valid) {
    return out_summary;
  }

  out_summary.pipeline_name = debug_set.pipeline_name;
  out_summary.stage_name = debug_set.stage_name;
  out_summary.previous_stage_name = debug_set.previous_stage_name;
  out_summary.next_stage_name = debug_set.next_stage_name;
  out_summary.ingress_bindings.reserve(debug_set.ingress_bindings.size());
  for (const agent::PipelineStageBridgeDebugBinding& binding : debug_set.ingress_bindings) {
    out_summary.ingress_bindings.push_back(debug_tool::PipelineStageBridgeDebugBinding{
      .source_stage_name = binding.source_stage_name,
      .target_stage_name = binding.target_stage_name,
      .source_container_name = binding.source_container_name,
      .target_container_name = binding.target_container_name,
      .required = binding.required,
    });
  }
  out_summary.egress_bindings.reserve(debug_set.egress_bindings.size());
  for (const agent::PipelineStageBridgeDebugBinding& binding : debug_set.egress_bindings) {
    out_summary.egress_bindings.push_back(debug_tool::PipelineStageBridgeDebugBinding{
      .source_stage_name = binding.source_stage_name,
      .target_stage_name = binding.target_stage_name,
      .source_container_name = binding.source_container_name,
      .target_container_name = binding.target_container_name,
      .required = binding.required,
    });
  }

  if (debug_set.has_stage_input_container_set) {
    out_summary.stage_input_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.stage_input_container_set, "bridge_stage_input"));
    out_summary.logical_decomposer_snapshot = out_summary.stage_input_reflection_snapshot;
    out_summary.has_logical_decomposer_snapshot = true;
    out_summary.has_stage_input_reflection_snapshot = true;
  }
  if (debug_set.has_stage_output_container_set) {
    out_summary.stage_output_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.stage_output_container_set, "bridge_stage_output"));
    out_summary.stage_runtime_snapshot = out_summary.stage_output_reflection_snapshot;
    out_summary.has_stage_runtime_snapshot = true;
    out_summary.has_stage_output_reflection_snapshot = true;
  }
  if (debug_set.has_next_stage_input_container_set) {
    out_summary.next_stage_input_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.next_stage_input_container_set, "bridge_next_stage_input"));
    out_summary.logical_reflector_snapshot = out_summary.next_stage_input_reflection_snapshot;
    out_summary.has_logical_reflector_snapshot = true;
    out_summary.has_next_stage_input_reflection_snapshot = true;
  }
  if (debug_set.has_replay_output_container_set) {
    out_summary.replay_output_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(
        _BuildBridgeReflectionSnapshot(debug_set.replay_output_container_set, "bridge_replay_output"));
    out_summary.logical_replay_reflector_snapshot = out_summary.replay_output_reflection_snapshot;
    out_summary.has_logical_replay_reflector_snapshot = true;
    out_summary.has_replay_output_reflection_snapshot = true;
  }
  if (debug_set.replay_reflection_snapshot.valid) {
    out_summary.replay_reflection_snapshot =
      _ToDebugToolReflectionSnapshot(debug_set.replay_reflection_snapshot);
  }
  out_summary.replay_valid = debug_set.replay_valid;
  out_summary.valid = true;
  return out_summary;
}

debug_tool::AlgorithmRuntimeSummary _ToDebugToolAlgorithmRuntimeSummary(
  const agent::Agent& managed_agent,
  size_t algorithm_index) {
  debug_tool::AlgorithmRuntimeSummary summary{};
  const agent::AlgorithmObject* object = managed_agent.algorithm_object(algorithm_index);
  const agent::AgentAlgorithmRuntimeState* runtime_state = managed_agent.algorithm_runtime_state(algorithm_index);
  if (!object) {
    return summary;
  }

  summary.algorithm_name = object->algorithm_profile.algorithm_name;
  summary.assembly_state = _ToDebugToolAlgorithmAssemblyState(managed_agent.algorithm_assembly_state(algorithm_index));
  summary.pipeline_name = object->pipeline_name;
  summary.pipeline_stage_index = object->pipeline_stage_index;
  summary.pipeline_stage_count = object->pipeline_stage_count;
  summary.pipeline_stage = object->pipeline_stage;
  summary.pipeline_topology =
    static_cast<debug_tool::AlgorithmPipelineTopology>(
      static_cast<int>(object->pipeline_topology));
  summary.pipeline_sync_mode =
    static_cast<debug_tool::AlgorithmPipelineSyncMode>(
      static_cast<int>(object->pipeline_sync_mode));
  summary.resource_bindings.reserve(object->resource_bindings.size());
  for (const agent::AlgorithmResourceBinding& binding : object->resource_bindings) {
    summary.resource_bindings.push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  summary.descriptor_values.reserve(object->descriptor_values.size());
  for (const agent::AlgorithmDescriptorValue& value : object->descriptor_values) {
    summary.descriptor_values.push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  summary.cpu_symbol = object->cpu_symbol;
  summary.gpu_symbol = object->gpu_symbol;
  summary.has_reflector = object->algorithm_reflector != nullptr;
  summary.has_intervention = object->intervention != nullptr;
  summary.mount_mode = static_cast<debug_tool::AlgorithmMountMode>(static_cast<int>(object->mount_mode));
  summary.execution_preference =
    static_cast<debug_tool::AlgorithmExecutionPreference>(static_cast<int>(object->execution_preference));
  if (runtime_state) {
    summary.agent_to_algorithm_signal = runtime_state->agent_to_algorithm_signal;
    summary.algorithm_to_agent_signal = runtime_state->algorithm_to_agent_signal;
    summary.pipeline_active_stage_index = runtime_state->pipeline_active_stage_index;
    summary.pipeline_active_stage_index_valid = runtime_state->pipeline_active_stage_index_valid;
    summary.pipeline_total_elapsed_seconds = runtime_state->pipeline_total_elapsed_seconds;
    summary.pipeline_stage_runtime_stats = runtime_state->pipeline_stage_runtime_stats;
  }
  const agent::AlgorithmReflectionSnapshot* reflection_snapshot = nullptr;
  algorithm_management::CpuPipelineRuntimeState pipeline_runtime_state{};
  if (object->pipeline_stage &&
      object->pipeline_stage_index == 0u &&
      !object->pipeline_name.empty() &&
      algorithm_management::TryGetMountedPipelineRuntime(
        object->pipeline_name,
        managed_agent.agent_name(),
        &pipeline_runtime_state) &&
      pipeline_runtime_state.exit_reflection_snapshot_valid) {
    reflection_snapshot = &pipeline_runtime_state.exit_reflection_snapshot;
  } else if (runtime_state && runtime_state->reflection_snapshot.valid) {
    reflection_snapshot = &runtime_state->reflection_snapshot;
  }
  if (reflection_snapshot) {
    summary.reflection_snapshot = _ToDebugToolReflectionSnapshot(*reflection_snapshot);
  }
  if (object->intervention) {
    std::vector<algorithm_management::AlgorithmInterventionStageSpec> stage_specs;
    if (object->intervention->GetInterventionStageSpecs(&stage_specs)) {
      summary.intervention_stage_summaries.reserve(stage_specs.size());
      for (const algorithm_management::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
        debug_tool::AlgorithmInterventionStageSummary stage_summary{};
        stage_summary.stage_name = stage_spec.stage_name;
        stage_summary.stage_kind = stage_spec.stage_kind;
        stage_summary.functions = stage_spec.functions;
        stage_summary.used_algorithm_containers = stage_spec.used_algorithm_containers;
        stage_summary.vertex_shader_path = stage_spec.shader.vertex_shader_path;
        stage_summary.fragment_shader_path = stage_spec.shader.fragment_shader_path;
        stage_summary.pipeline_kind = stage_spec.shader.pipeline_kind;
        summary.intervention_stage_summaries.push_back(std::move(stage_summary));
      }
    }
  }
  if (runtime_state && runtime_state->bridge_debug_set.valid) {
    summary.bridge_debug_set = _ToDebugToolPipelineStageBridgeDebugSummary(runtime_state->bridge_debug_set);
  }
  return summary;
}

std::vector<agent::AlgorithmResourceBinding> _ToAgentResourceBindings(
  const std::vector<debug_tool::AlgorithmResourceBinding>& bindings) {
  std::vector<agent::AlgorithmResourceBinding> result;
  result.reserve(bindings.size());
  for (const debug_tool::AlgorithmResourceBinding& binding : bindings) {
    result.push_back(agent::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
    });
  }
  return result;
}

std::vector<agent::AlgorithmDescriptorValue> _ToAgentDescriptorValues(
  const std::vector<debug_tool::AlgorithmDescriptorValue>& values) {
  std::vector<agent::AlgorithmDescriptorValue> result;
  result.reserve(values.size());
  for (const debug_tool::AlgorithmDescriptorValue& value : values) {
    result.push_back(agent::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  return result;
}

std::vector<agent::AlgorithmPipelineStageSubmission> _ToAgentPipelineStageSubmissions(
  const std::vector<debug_tool::AlgorithmPipelineStageSubmission>& stage_submissions) {
  std::vector<agent::AlgorithmPipelineStageSubmission> result;
  result.reserve(stage_submissions.size());
  for (const debug_tool::AlgorithmPipelineStageSubmission& stage_submission : stage_submissions) {
    result.push_back(agent::AlgorithmPipelineStageSubmission{
      .stage_name = stage_submission.stage_name,
      .resource_bindings = _ToAgentResourceBindings(stage_submission.resource_bindings),
      .descriptor_values = _ToAgentDescriptorValues(stage_submission.descriptor_values),
    });
  }
  return result;
}

std::vector<debug_tool::AlgorithmPipelineStageSubmission> _MakeSingleStagePipelineSubmissions(
  const std::string& pipeline_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values) {
  std::vector<debug_tool::AlgorithmPipelineStageSubmission> stage_submissions;
  stage_submissions.push_back(debug_tool::AlgorithmPipelineStageSubmission{
    .stage_name = pipeline_name,
    .resource_bindings = resource_bindings,
    .descriptor_values = descriptor_values,
  });
  return stage_submissions;
}

bool _IsPipelineResourceBatchSubmissionName(const std::string& pipeline_name) {
  return pipeline_name.find("::testsubmit") != std::string::npos;
}

bool _FindMountedPipelineInstanceName(
  const std::shared_ptr<agent::Agent>& managed_agent,
  const std::string& pipeline_algorithm_name,
  std::string* out_pipeline_name,
  size_t* out_pipeline_index,
  std::string* out_error_message) {
  if (!managed_agent) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline agent is unavailable.";
    }
    return false;
  }
  if (!out_pipeline_name) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline name output pointer is null.";
    }
    return false;
  }

  out_pipeline_name->clear();
  if (out_pipeline_index) {
    *out_pipeline_index = 0u;
  }

  size_t matched_index = 0u;
  bool matched = false;
  for (size_t i = 0u; i < managed_agent->algorithm_count(); ++i) {
    const agent::AlgorithmObject* object = managed_agent->algorithm_object(i);
    if (!object ||
        !object->pipeline_stage ||
        object->pipeline_stage_index != 0u ||
        object->algorithm_profile.algorithm_name != pipeline_algorithm_name ||
        object->pipeline_topology != agent::AlgorithmPipelineTopology::Circular ||
        _IsPipelineResourceBatchSubmissionName(object->pipeline_name)) {
      continue;
    }
    if (matched) {
      if (out_error_message) {
        *out_error_message =
          "Multiple mounted pipeline instances match the selected algorithm. Reset to a single mounted pipeline first.";
      }
      return false;
    }
    matched = true;
    matched_index = i;
    *out_pipeline_name = object->pipeline_name;
  }

  if (!matched) {
    if (out_error_message) {
      *out_error_message = "Mount the pipeline before submitting a resource batch.";
    }
    return false;
  }

  if (out_pipeline_index) {
    *out_pipeline_index = matched_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

std::vector<debug_tool::AlgorithmResourceBinding> _ToDebugToolResourceBindings(
  const std::vector<agent::AlgorithmResourceBinding>& bindings) {
  std::vector<debug_tool::AlgorithmResourceBinding> result;
  result.reserve(bindings.size());
  for (const agent::AlgorithmResourceBinding& binding : bindings) {
    result.push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  return result;
}

std::vector<debug_tool::AlgorithmDescriptorValue> _ToDebugToolDescriptorValues(
  const std::vector<agent::AlgorithmDescriptorValue>& values) {
  std::vector<debug_tool::AlgorithmDescriptorValue> result;
  result.reserve(values.size());
  for (const agent::AlgorithmDescriptorValue& value : values) {
    result.push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  return result;
}

bool _BuildPipelineStageSubmissionsFromPackage(
  const std::string& pipeline_algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& stage0_resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& stage0_descriptor_values,
  bool include_stage0_bindings,
  std::vector<debug_tool::AlgorithmPipelineStageSubmission>* out_stage_submissions,
  std::string* out_error_message) {
  if (!out_stage_submissions) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage submission output pointer is null.";
    }
    return false;
  }

  out_stage_submissions->clear();
  if (pipeline_algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline algorithm name must not be empty.";
    }
    return false;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        pipeline_algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + pipeline_algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map{};
  bool has_transfer_map = false;
  std::string transfer_map_error_message;
  if (!algorithm_management::LoadAlgorithmPackageTransferMapFromLocation(
        package_location,
        &transfer_map,
        &has_transfer_map,
        &transfer_map_error_message)) {
    if (out_error_message) {
      *out_error_message = transfer_map_error_message.empty()
        ? ("Failed to load runtime transfer map for '" + pipeline_algorithm_name + "'.")
        : std::move(transfer_map_error_message);
    }
    return false;
  }

  if (!has_transfer_map || !transfer_map || transfer_map->empty()) {
    *out_stage_submissions = _MakeSingleStagePipelineSubmissions(
      pipeline_algorithm_name,
      include_stage0_bindings ? stage0_resource_bindings : std::vector<debug_tool::AlgorithmResourceBinding>{},
      include_stage0_bindings ? stage0_descriptor_values : std::vector<debug_tool::AlgorithmDescriptorValue>{});
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_stage_submissions->push_back(debug_tool::AlgorithmPipelineStageSubmission{
    .stage_name = pipeline_algorithm_name,
    .resource_bindings = include_stage0_bindings ? stage0_resource_bindings : std::vector<debug_tool::AlgorithmResourceBinding>{},
    .descriptor_values = include_stage0_bindings ? stage0_descriptor_values : std::vector<debug_tool::AlgorithmDescriptorValue>{},
  });

  std::unordered_set<std::string> visited_stage_names{};
  visited_stage_names.insert(pipeline_algorithm_name);
  std::string stage_name = pipeline_algorithm_name;
  while (true) {
    const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
      transfer_map->FindOutgoingEdges(stage_name);
    if (outgoing_edges.empty()) {
      break;
    }
    if (outgoing_edges.size() != 1u || !outgoing_edges.front()) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map for '" + pipeline_algorithm_name + "' is not linear.";
      }
      return false;
    }

    const std::string next_stage_name = outgoing_edges.front()->target_stage_name;
    if (next_stage_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map for '" + pipeline_algorithm_name + "' contains an empty stage name.";
      }
      return false;
    }
    if (!visited_stage_names.insert(next_stage_name).second) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map for '" + pipeline_algorithm_name + "' contains a cycle at '" +
          next_stage_name + "'.";
      }
      return false;
    }

    ::algorithm::AlgorithmPackageLocation next_package_location{};
    std::string next_location_error_message;
    if (!algorithm_management::TryResolveAlgorithmPackageLocation(
          next_stage_name,
          &next_package_location,
          &next_location_error_message)) {
      if (out_error_message) {
        *out_error_message = next_location_error_message.empty()
          ? ("Failed to resolve algorithm package location for '" + next_stage_name + "'.")
          : std::move(next_location_error_message);
      }
      return false;
    }

    std::vector<agent::AlgorithmResourceBinding> default_resource_bindings{};
    std::vector<agent::AlgorithmDescriptorValue> default_descriptor_values{};
    bool has_default_file = false;
    std::string default_error_message;
    if (!algorithm_management::LoadAlgorithmPackageDefaultBindingsFromLocation(
          next_package_location,
          &default_resource_bindings,
          &default_descriptor_values,
          &has_default_file,
          &default_error_message)) {
      if (out_error_message) {
        *out_error_message = default_error_message.empty()
          ? ("Failed to load default bindings for '" + next_stage_name + "'.")
          : std::move(default_error_message);
      }
      return false;
    }

    out_stage_submissions->push_back(debug_tool::AlgorithmPipelineStageSubmission{
      .stage_name = next_stage_name,
      .resource_bindings = _ToDebugToolResourceBindings(default_resource_bindings),
      .descriptor_values = _ToDebugToolDescriptorValues(default_descriptor_values),
    });
    stage_name = next_stage_name;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _CollectPipelineStageSubmissionsFromSummary(
  const debug_tool::AgentRuntimeSummary& agent_summary,
  const std::string& pipeline_name,
  std::vector<debug_tool::AlgorithmPipelineStageSubmission>* out_stage_submissions,
  std::string* out_error_message) {
  if (!out_stage_submissions) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage submission output pointer is null.";
    }
    return false;
  }

  out_stage_submissions->clear();
  if (pipeline_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline name must not be empty.";
    }
    return false;
  }

  std::vector<const debug_tool::AlgorithmRuntimeSummary*> pipeline_stages;
  for (const debug_tool::AlgorithmRuntimeSummary& algorithm_summary : agent_summary.algorithms) {
    if (!algorithm_summary.pipeline_stage || algorithm_summary.pipeline_name != pipeline_name) {
      continue;
    }
    pipeline_stages.push_back(&algorithm_summary);
  }

  if (pipeline_stages.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline group is unavailable for '" + pipeline_name + "'.";
    }
    return false;
  }

  std::sort(
    pipeline_stages.begin(),
    pipeline_stages.end(),
    [](const debug_tool::AlgorithmRuntimeSummary* lhs, const debug_tool::AlgorithmRuntimeSummary* rhs) {
      return lhs && rhs
        ? lhs->pipeline_stage_index < rhs->pipeline_stage_index
        : lhs != nullptr;
    });

  out_stage_submissions->reserve(pipeline_stages.size());
  for (const debug_tool::AlgorithmRuntimeSummary* stage_summary : pipeline_stages) {
    if (!stage_summary) {
      if (out_error_message) {
        *out_error_message = "Pipeline group contains an unreadable stage.";
      }
      return false;
    }
    out_stage_submissions->push_back(debug_tool::AlgorithmPipelineStageSubmission{
      .stage_name = stage_summary->algorithm_name,
      .resource_bindings = stage_summary->resource_bindings,
      .descriptor_values = stage_summary->descriptor_values,
    });
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _IsPipelineAlgorithmByName(
  const std::string& algorithm_name,
  bool* out_is_pipeline,
  std::string* out_error_message) {
  if (!out_is_pipeline) {
    if (out_error_message) {
      *out_error_message = "Pipeline query output pointer is null.";
    }
    return false;
  }

  *out_is_pipeline = false;
  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map{};
  bool has_transfer_map = false;
  std::string transfer_map_error_message;
  if (!algorithm_management::LoadAlgorithmPackageTransferMapFromLocation(
        package_location,
        &transfer_map,
        &has_transfer_map,
        &transfer_map_error_message)) {
    if (out_error_message) {
      *out_error_message = transfer_map_error_message.empty()
        ? ("Failed to load runtime transfer map for '" + algorithm_name + "'.")
        : std::move(transfer_map_error_message);
    }
    return false;
  }

  *out_is_pipeline = has_transfer_map && transfer_map && !transfer_map->empty();
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

DebugToolBackendRuntime::~DebugToolBackendRuntime() {
  Destroy();
}

bool DebugToolBackendRuntime::Init(const char* window_title, int width, int height) {
  agent_manager_.Destroy();
  ui_status_message_.clear();
  if (!runtime_environment_.Init(window_title ? window_title : "debugTool", width, height)) {
    return false;
  }

  AgentCreateSpec default_agent_spec{};
  default_agent_spec.agent_name = "debug_agent";
  default_agent_spec.limit_fps_flag = common_data::DefaultAgentLimitFpsFlag();
  if (!CreateAgent(default_agent_spec.agent_name.c_str(), default_agent_spec.limit_fps_flag)) {
    runtime_environment_.Destroy();
    return false;
  }

  PauseTicking();
  frame_dt_ = 0.0f;
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool DebugToolBackendRuntime::Tick() {
  if (!runtime_environment_.has_window()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  if (!agent_manager_.Tick(runtime_environment_.input(), runtime_environment_.MousePosition(), frame_dt_)) {
    return false;
  }
  return runtime_environment_.Tick();
}

void DebugToolBackendRuntime::Destroy() {
  agent_manager_.Destroy();
  runtime_environment_.Destroy();
  ui_status_message_.clear();
  last_frame_time_ = {};
  frame_dt_ = 0.0f;
}

bool DebugToolBackendRuntime::CreateAgent(const char* agent_name, uint32_t limit_fps_flag, size_t* out_agent_index) {
  AgentCreateSpec spec{};
  spec.agent_name = agent_name ? agent_name : "";
  spec.limit_fps_flag = limit_fps_flag;
  return agent_manager_.CreateAgent(std::move(spec), out_agent_index);
}

bool DebugToolBackendRuntime::has_agents() const {
  return agent_manager_.has_agents();
}

size_t DebugToolBackendRuntime::agent_count() const {
  return agent_manager_.agent_count();
}

bool DebugToolBackendRuntime::AttachAlgorithmToAgent(
  size_t agent_index,
  const std::string& algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmMountMode mount_mode,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  if (mount_mode == debug_tool::AlgorithmMountMode::Pipeline) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline algorithms must be mounted through the dedicated pipeline path, not AttachAlgorithmToAgent.";
    }
    DEBUG_TOOL_ASSERT(false, "Pipeline mount was routed through the normal algorithm attach path.");
    return false;
  }

  bool is_pipeline = false;
  std::string pipeline_error_message;
  if (!algorithm_name.empty() &&
      !IsPipelineAlgorithm(algorithm_name, &is_pipeline, &pipeline_error_message)) {
    if (out_error_message) {
      *out_error_message = pipeline_error_message.empty()
        ? ("Failed to resolve algorithm type for '" + algorithm_name + "'.")
        : std::move(pipeline_error_message);
    }
    DEBUG_TOOL_ASSERT(false, "Failed to resolve algorithm type for algorithm attachment.");
    return false;
  }

  if (is_pipeline) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline algorithms must use pipeline mount + resource submission, not the normal algorithm attach path.";
    }
    DEBUG_TOOL_ASSERT(false, "Pipeline algorithm was submitted through the normal algorithm attach path.");
    return false;
  }

  const bool attached = agent_manager_.AttachAlgorithmToAgent(
    agent_index,
    algorithm_name,
    _ToAgentResourceBindings(resource_bindings),
    _ToAgentDescriptorValues(descriptor_values),
    out_algorithm_index,
    out_error_message,
    static_cast<agent::AlgorithmMountMode>(static_cast<int>(mount_mode)),
    static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)));
  if (!attached && out_error_message && out_error_message->empty()) {
    *out_error_message = "Failed to attach algorithm to the built-in agent.";
  }
  return attached;
}

bool DebugToolBackendRuntime::AttachPipelinePackageToAgent(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::string& pipeline_algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  const bool include_stage0_bindings = _IsPipelineResourceBatchSubmissionName(pipeline_name);
  if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
    _AppendPipelineRunnerProbe(
      "backend_attach_probe.log",
      "attach.begin include_stage0=" + std::string(include_stage0_bindings ? "true" : "false") +
        " pipeline=" + pipeline_name +
        " algorithm=" + pipeline_algorithm_name);
  }
  if (include_stage0_bindings) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    std::string mounted_pipeline_name{};
    size_t mounted_pipeline_index = 0u;
    if (!_FindMountedPipelineInstanceName(
          managed_agent,
          pipeline_algorithm_name,
          &mounted_pipeline_name,
          &mounted_pipeline_index,
          out_error_message)) {
      DEBUG_TOOL_ASSERT(false, "Failed to resolve the mounted pipeline instance for resource submission.");
      return false;
    }
    if (!agent_manager_.EnqueuePipelineStage0Submission(
          agent_index,
          mounted_pipeline_name,
          _ToAgentResourceBindings(resource_bindings),
          _ToAgentDescriptorValues(descriptor_values),
          out_error_message)) {
      DEBUG_TOOL_ASSERT(false, "Failed to enqueue the pipeline resource batch.");
      return false;
    }
    if (out_algorithm_index) {
      *out_algorithm_index = mounted_pipeline_index;
    }
    return true;
  }

  std::vector<debug_tool::AlgorithmPipelineStageSubmission> stage_submissions{};
  if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
    _AppendPipelineRunnerProbe("backend_attach_probe.log", "attach.build_stage_submissions.begin");
  }
  if (!_BuildPipelineStageSubmissionsFromPackage(
        pipeline_algorithm_name,
        resource_bindings,
        descriptor_values,
        include_stage0_bindings,
        &stage_submissions,
        out_error_message)) {
    DEBUG_TOOL_ASSERT(false, "Failed to expand pipeline stage submissions.");
    return false;
  }
  if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
    _AppendPipelineRunnerProbe(
      "backend_attach_probe.log",
      "attach.build_stage_submissions.end count=" + std::to_string(stage_submissions.size()));
    for (size_t i = 0; i < stage_submissions.size(); ++i) {
      _AppendPipelineRunnerProbe(
        "backend_attach_probe.log",
        "attach.stage_submission[" + std::to_string(i) + "]=" + stage_submissions[i].stage_name);
    }
  }

  size_t attached_algorithm_index = 0u;
  if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
    _AppendPipelineRunnerProbe("backend_attach_probe.log", "attach.agent_manager_mount.begin");
  }
  const bool attached = agent_manager_.AttachPipelineAlgorithmToAgent(
    agent_index,
    pipeline_name,
    _ToAgentPipelineStageSubmissions(stage_submissions),
    &attached_algorithm_index,
    out_error_message,
    static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)),
    agent::AlgorithmPipelineTopology::Circular,
    agent::AlgorithmPipelineSyncMode::Forced);
  if (!attached) {
    DEBUG_TOOL_ASSERT(false, "Failed to attach pipeline package to the built-in agent.");
    return false;
  }
  if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
    _AppendPipelineRunnerProbe(
      "backend_attach_probe.log",
      "attach.agent_manager_mount.end index=" + std::to_string(attached_algorithm_index));
  }

  if (out_algorithm_index) {
    *out_algorithm_index = attached_algorithm_index;
  }

  if (!include_stage0_bindings) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    DEBUG_TOOL_ASSERT(managed_agent != nullptr, "Mounted pipeline agent is unavailable.");
    if (!managed_agent) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline agent is unavailable.";
      }
      return false;
    }
    const agent::AlgorithmObject* root_stage =
      managed_agent->algorithm_object(attached_algorithm_index);
    DEBUG_TOOL_ASSERT(root_stage != nullptr, "Mounted pipeline root stage is unavailable.");
    if (!root_stage) {
      if (out_error_message) {
        *out_error_message = "Mounted pipeline root stage is unavailable.";
      }
      return false;
    }
    const uint32_t pipeline_stage_count = root_stage->pipeline_stage_count;
    if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
      _AppendPipelineRunnerProbe(
        "backend_attach_probe.log",
        "attach.mark_waiting.begin stage_count=" + std::to_string(pipeline_stage_count));
    }
    for (uint32_t stage_offset = 0u; stage_offset < pipeline_stage_count; ++stage_offset) {
      const size_t stage_index = attached_algorithm_index + static_cast<size_t>(stage_offset);
      const bool marked_waiting = managed_agent->BeginAlgorithmAssembly(stage_index);
      DEBUG_TOOL_ASSERT(
        marked_waiting,
        "Failed to mark mounted pipeline stage as waiting for resource submission.");
      if (!marked_waiting) {
        if (out_error_message) {
          *out_error_message = "Failed to mark mounted pipeline stage as waiting for resource submission.";
        }
        return false;
      }
    }
    if (_ShouldEmitPipelineRunnerProbe(pipeline_name)) {
      _AppendPipelineRunnerProbe("backend_attach_probe.log", "attach.mark_waiting.end");
    }
  }
  return attached;
}

bool DebugToolBackendRuntime::IsPipelineAlgorithm(
  const std::string& algorithm_name,
  bool* out_is_pipeline,
  std::string* out_error_message) const {
  return _IsPipelineAlgorithmByName(algorithm_name, out_is_pipeline, out_error_message);
}

bool DebugToolBackendRuntime::AttachPipelineAlgorithmToAgent(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::vector<debug_tool::AlgorithmPipelineStageSubmission>& stage_submissions,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  const bool attached = agent_manager_.AttachPipelineAlgorithmToAgent(
    agent_index,
    pipeline_name,
    _ToAgentPipelineStageSubmissions(stage_submissions),
    out_algorithm_index,
    out_error_message,
    static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)),
    agent::AlgorithmPipelineTopology::NonCircular,
    agent::AlgorithmPipelineSyncMode::Forced);
  if (!attached) {
    DEBUG_TOOL_ASSERT(false, "Failed to attach pipeline algorithm to the built-in agent.");
  }
  return attached;
}

bool DebugToolBackendRuntime::ReplayPipelineStageBridgeDebug(
  size_t agent_index,
  size_t algorithm_index,
  std::string* out_error_message) {
  const agent::AgentTickContext context{
    .input = &runtime_environment_.input(),
    .mouse_pixel = runtime_environment_.MousePosition(),
    .dt_seconds = frame_dt_,
    .intervention_request = nullptr,
  };
  return agent_manager_.ReplayPipelineStageBridgeDebug(
    agent_index,
    algorithm_index,
    context,
    out_error_message);
}

bool DebugToolBackendRuntime::HotReloadAlgorithmPackage(
  size_t agent_index,
  size_t algorithm_index,
  size_t* out_algorithm_index,
  std::string* out_error_message) {
  if (out_algorithm_index) {
    *out_algorithm_index = 0u;
  }

  debug_tool::AgentRuntimeSummary agent_summary{};
  if (!GetAgentSummary(agent_index, &agent_summary)) {
    if (out_error_message) {
      *out_error_message = "Selected agent is unavailable.";
    }
    return false;
  }
  if (algorithm_index >= agent_summary.algorithms.size()) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm is unavailable.";
    }
    return false;
  }

  const debug_tool::AlgorithmRuntimeSummary algorithm_summary =
    agent_summary.algorithms[algorithm_index];
  if (algorithm_summary.algorithm_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm has no name.";
    }
    return false;
  }

  const bool pipeline_algorithm = algorithm_summary.pipeline_stage && !algorithm_summary.pipeline_name.empty();
  std::vector<debug_tool::AlgorithmPipelineStageSubmission> pipeline_stage_submissions;
  size_t pipeline_selected_stage_offset = 0u;
  if (pipeline_algorithm) {
    bool found_pipeline_begin = false;
    size_t pipeline_begin_index = 0u;
    for (size_t i = 0; i < agent_summary.algorithms.size(); ++i) {
      const debug_tool::AlgorithmRuntimeSummary& stage_summary = agent_summary.algorithms[i];
      if (!stage_summary.pipeline_stage || stage_summary.pipeline_name != algorithm_summary.pipeline_name) {
        continue;
      }
      if (!found_pipeline_begin) {
        pipeline_begin_index = i;
        found_pipeline_begin = true;
      }
      if (i == algorithm_index) {
        pipeline_selected_stage_offset = i - pipeline_begin_index;
      }
    }
  }
  if (pipeline_algorithm &&
      !_CollectPipelineStageSubmissionsFromSummary(
        agent_summary,
        algorithm_summary.pipeline_name,
        &pipeline_stage_submissions,
        out_error_message)) {
    return false;
  }

  const bool was_ticking = agent_manager_.tick_enabled();
  if (was_ticking) {
    agent_manager_.PauseTicking();
  }

  std::string detach_error_message;
  if (!agent_manager_.DetachAlgorithmFromAgent(agent_index, algorithm_index, &detach_error_message)) {
    if (was_ticking) {
      agent_manager_.StartTicking();
    }
    if (out_error_message) {
      *out_error_message = detach_error_message.empty()
        ? ("Failed to detach algorithm '" + algorithm_summary.algorithm_name + "'.")
        : std::move(detach_error_message);
    }
    return false;
  }

  runtime_environment_.ClearGpuRuntimeCaches();
  runtime_environment_.SetRenderPreviewRequest({});

  const std::string build_command = _HotReloadBuildCommand(algorithm_summary.algorithm_name);
  const int build_result = std::system(build_command.c_str());
  if (build_result != 0) {
    size_t restored_algorithm_index = 0u;
    std::string restore_error_message;
    const bool restored = pipeline_algorithm
      ? agent_manager_.AttachPipelineAlgorithmToAgent(
          agent_index,
          algorithm_summary.pipeline_name,
          _ToAgentPipelineStageSubmissions(pipeline_stage_submissions),
          &restored_algorithm_index,
          &restore_error_message,
          static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(algorithm_summary.execution_preference)),
          static_cast<agent::AlgorithmPipelineTopology>(static_cast<int>(algorithm_summary.pipeline_topology)),
          static_cast<agent::AlgorithmPipelineSyncMode>(static_cast<int>(algorithm_summary.pipeline_sync_mode)))
      : agent_manager_.AttachAlgorithmToAgent(
          agent_index,
          algorithm_summary.algorithm_name,
          _ToAgentResourceBindings(algorithm_summary.resource_bindings),
          _ToAgentDescriptorValues(algorithm_summary.descriptor_values),
          &restored_algorithm_index,
          &restore_error_message,
          static_cast<agent::AlgorithmMountMode>(static_cast<int>(algorithm_summary.mount_mode)),
          static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(algorithm_summary.execution_preference)));
    if (!restored) {
      if (out_error_message) {
        *out_error_message = restore_error_message.empty()
          ? ("Hot reload failed and the old algorithm could not be restored for '" +
            (pipeline_algorithm ? algorithm_summary.pipeline_name : algorithm_summary.algorithm_name) + "'.")
          : std::move(restore_error_message);
      }
    } else if (out_error_message) {
      *out_error_message =
        "Hot reload build failed for '" + algorithm_summary.algorithm_name + "'.";
    }

    runtime_environment_.ClearGpuRuntimeCaches();
    runtime_environment_.SetRenderPreviewRequest({});

    if (out_algorithm_index) {
      *out_algorithm_index = pipeline_algorithm
        ? restored_algorithm_index + pipeline_selected_stage_offset
        : restored_algorithm_index;
    }
    if (was_ticking) {
      agent_manager_.StartTicking();
    }
    return false;
  }

  size_t rebuilt_algorithm_index = 0u;
  std::string attach_error_message;
  const bool rebuilt = pipeline_algorithm
    ? agent_manager_.AttachPipelineAlgorithmToAgent(
        agent_index,
        algorithm_summary.pipeline_name,
        _ToAgentPipelineStageSubmissions(pipeline_stage_submissions),
        &rebuilt_algorithm_index,
        &attach_error_message,
        static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(algorithm_summary.execution_preference)),
        static_cast<agent::AlgorithmPipelineTopology>(static_cast<int>(algorithm_summary.pipeline_topology)),
        static_cast<agent::AlgorithmPipelineSyncMode>(static_cast<int>(algorithm_summary.pipeline_sync_mode)))
    : agent_manager_.AttachAlgorithmToAgent(
        agent_index,
        algorithm_summary.algorithm_name,
        _ToAgentResourceBindings(algorithm_summary.resource_bindings),
        _ToAgentDescriptorValues(algorithm_summary.descriptor_values),
        &rebuilt_algorithm_index,
        &attach_error_message,
        static_cast<agent::AlgorithmMountMode>(static_cast<int>(algorithm_summary.mount_mode)),
        static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(algorithm_summary.execution_preference)));
  if (!rebuilt) {
    if (out_error_message) {
      *out_error_message = attach_error_message.empty()
        ? ("Hot reload succeeded, but the updated algorithm could not be reattached for '" +
          (pipeline_algorithm ? algorithm_summary.pipeline_name : algorithm_summary.algorithm_name) + "'.")
        : std::move(attach_error_message);
    }
    runtime_environment_.ClearGpuRuntimeCaches();
    runtime_environment_.SetRenderPreviewRequest({});
    if (was_ticking) {
      agent_manager_.StartTicking();
    }
    return false;
  }

  runtime_environment_.ClearGpuRuntimeCaches();
  runtime_environment_.SetRenderPreviewRequest({});

  if (out_algorithm_index) {
    *out_algorithm_index = pipeline_algorithm
      ? rebuilt_algorithm_index + pipeline_selected_stage_offset
      : rebuilt_algorithm_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  if (was_ticking) {
    agent_manager_.StartTicking();
  }
  return true;
}

bool DebugToolBackendRuntime::GetAgentSummary(
  size_t agent_index,
  debug_tool::AgentRuntimeSummary* out_summary) const {
  if (!out_summary) {
    return false;
  }

  *out_summary = {};

  const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    return false;
  }

  out_summary->agent_name = managed_agent->agent_name();
  out_summary->algorithms.reserve(managed_agent->algorithm_count());
  for (size_t algorithm_index = 0; algorithm_index < managed_agent->algorithm_count(); ++algorithm_index) {
    out_summary->algorithms.push_back(_ToDebugToolAlgorithmRuntimeSummary(*managed_agent, algorithm_index));
  }
  return true;
}

const AlgorithmToAgentSignal& DebugToolBackendRuntime::combined_algorithm_to_agent_signal() const {
  return agent_manager_.combined_algorithm_to_agent_signal();
}

bool DebugToolBackendRuntime::LoadAlgorithmCatalog(
  std::vector<debug_tool::AlgorithmCatalogEntry>* out_entries,
  std::string* out_error_message) const {
  if (!out_entries) {
    if (out_error_message) {
      *out_error_message = "Algorithm catalog output pointer is null.";
    }
    return false;
  }

  out_entries->clear();

  const std::string catalog_path = _AlgorithmCatalogPath();
  const std::string json_text = _ReadTextFile(catalog_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read algorithm catalog: " + catalog_path;
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse algorithm catalog: " + catalog_path;
    }
    return false;
  }

  const cJSON* algorithms = cJSON_GetObjectItemCaseSensitive(root, "algorithms");
  if (!algorithms || !cJSON_IsArray(algorithms)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Algorithm catalog is missing the algorithms array.";
    }
    return false;
  }

  const int count = cJSON_GetArraySize(algorithms);
  out_entries->reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(algorithms, i);
    if (!item || !cJSON_IsObject(item)) {
      continue;
    }

    debug_tool::AlgorithmCatalogEntry entry{};
    entry.algorithm_name = _GetJsonStringField(item, "name");
    entry.display_name = _GetJsonStringField(item, "display_name");
    entry.folder_name = _GetJsonStringField(item, "folder");
    entry.container_manifest_name = _GetJsonStringField(item, "container_manifest");
    entry.decomposer_name = _GetJsonStringField(item, "decomposer");
    entry.reflector_name = _GetJsonStringField(item, "reflector");
    entry.intervention_name = _GetJsonStringField(item, "intervention");

    if (entry.display_name.empty()) {
      entry.display_name = entry.algorithm_name;
    }
    if (!entry.algorithm_name.empty()) {
      out_entries->push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  if (out_entries->empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm catalog does not contain any entries.";
    }
    return false;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  std::vector<debug_tool::RequestedResourceEntry>* out_resources,
  std::vector<debug_tool::RequestedDescriptorEntry>* out_descriptors,
  std::string* out_error_message) const {
  if (!out_resources || !out_descriptors) {
    if (out_error_message) {
      *out_error_message = "Requested binding output pointers are null.";
    }
    return false;
  }

  out_resources->clear();
  out_descriptors->clear();

  algorithm_management::AlgorithmRequestedResources requested_resources{};
  algorithm_management::AlgorithmRequestedDescriptorBindings requested_descriptor_bindings{};
  std::string reflection_error_message;
  if (!algorithm_management::QueryAlgorithmRequestedBindings(
        algorithm_name,
        &requested_resources,
        &requested_descriptor_bindings,
        &reflection_error_message)) {
    if (out_error_message) {
      *out_error_message = reflection_error_message.empty()
        ? ("Failed to query requested bindings for '" + algorithm_name + "'.")
        : std::move(reflection_error_message);
    }
    return false;
  }

  for (const algorithm_management::AlgorithmRequestedResources::RequiredResource& resource :
       requested_resources.required_resources) {
    if (resource.resource_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested resource entry is missing a resource name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (resource.resource_kind.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested resource entry is missing a resource kind.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource kind for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    out_resources->push_back(debug_tool::RequestedResourceEntry{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .required = resource.required,
    });
  }
  for (const algorithm_management::AlgorithmRequestedDescriptorBindings::DescriptorSlot& descriptor :
       requested_descriptor_bindings.descriptor_slots) {
    if (descriptor.descriptor_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested descriptor entry is missing a descriptor name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a descriptor name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (descriptor.container_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested descriptor entry is missing a container name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a container name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    out_descriptors->push_back(debug_tool::RequestedDescriptorEntry{
      .descriptor_name = descriptor.descriptor_name,
      .container_name = descriptor.container_name,
      .array_index = descriptor.array_index,
    });
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<debug_tool::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<debug_tool::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) const {
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

  std::vector<algorithm_management::AlgorithmResourceBinding> package_resource_bindings;
  std::vector<algorithm_management::AlgorithmDescriptorValue> package_descriptor_values;
  bool has_default_file = false;
  std::string default_error_message;
  if (!algorithm_management::LoadAlgorithmPackageDefaultBindings(
        algorithm_name,
        &package_resource_bindings,
        &package_descriptor_values,
        &has_default_file,
        &default_error_message)) {
    if (out_error_message) {
      *out_error_message = default_error_message.empty()
        ? ("Failed to load default bindings for '" + algorithm_name + "'.")
        : std::move(default_error_message);
    }
    return false;
  }

  if (!has_default_file) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_resource_bindings->reserve(package_resource_bindings.size());
  for (const algorithm_management::AlgorithmResourceBinding& binding : package_resource_bindings) {
    out_resource_bindings->push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  out_descriptor_values->reserve(package_descriptor_values.size());
  for (const algorithm_management::AlgorithmDescriptorValue& value : package_descriptor_values) {
    out_descriptor_values->push_back(debug_tool::AlgorithmDescriptorValue{
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

bool DebugToolBackendRuntime::BuildRenderPreviewRequest(
  size_t agent_index,
  size_t algorithm_index,
  runtime_systems::RenderPreviewRequest* out_request,
  std::string* out_error_message) const {
  if (!out_request) {
    DEBUG_TOOL_ASSERT(false, "Preview request output pointer is null.");
    if (out_error_message) {
      *out_error_message = "Preview request output pointer is null.";
    }
    return false;
  }

  out_request->Clear();

  const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    DEBUG_TOOL_ASSERT(false, "Managed agent is unavailable.");
    if (out_error_message) {
      *out_error_message = "Managed agent is unavailable.";
    }
    return false;
  }

  const agent::AlgorithmObject* object = managed_agent->algorithm_object(algorithm_index);
  if (!object || !object->intervention) {
    if (out_error_message) {
      *out_error_message = "Algorithm has no intervention for preview rendering.";
    }
    return false;
  }
  const std::string algorithm_name = object->algorithm_profile.algorithm_name;

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!object->intervention->GetInterventionStageSpecs(&stage_specs) || stage_specs.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm intervention did not expose any stages.";
    }
    return false;
  }

  const agent::AlgorithmInterventionStageSpec* result_stage = nullptr;
  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      result_stage = &stage_spec;
      break;
    }
  }
  if (!result_stage) {
    if (out_error_message) {
      *out_error_message = "Algorithm intervention did not expose a result-render stage.";
    }
    return false;
  }
  if (result_stage->shader.vertex_shader_path.empty() || result_stage->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Result-render stage is missing shader paths.";
    }
    return false;
  }

  const AlgorithmContainerSet* container_set = object->container_set();
  if (!container_set) {
    DEBUG_TOOL_ASSERT(false, "Algorithm container set is unavailable for preview rendering.");
    if (out_error_message) {
      *out_error_message = "Algorithm container set is unavailable.";
    }
    return false;
  }

  const agent::AlgorithmReflectionSnapshot* reflection_snapshot = nullptr;
  algorithm_management::CpuPipelineRuntimeState pipeline_runtime_state{};
  if (object->pipeline_stage &&
      object->pipeline_stage_index == 0u &&
      !object->pipeline_name.empty() &&
      algorithm_management::TryGetMountedPipelineRuntime(
        object->pipeline_name,
        managed_agent->agent_name(),
        &pipeline_runtime_state) &&
      pipeline_runtime_state.exit_reflection_snapshot_valid) {
    reflection_snapshot = &pipeline_runtime_state.exit_reflection_snapshot;
  } else if (managed_agent->algorithm_runtime_state(algorithm_index) &&
      managed_agent->algorithm_runtime_state(algorithm_index)->reflection_snapshot.valid) {
    reflection_snapshot = &managed_agent->algorithm_runtime_state(algorithm_index)->reflection_snapshot;
  }

  out_request->stage_name = result_stage->stage_name;
  out_request->vertex_shader_path = _ResolveAlgorithmShaderPath(algorithm_name, result_stage->shader.vertex_shader_path);
  out_request->fragment_shader_path = _ResolveAlgorithmShaderPath(algorithm_name, result_stage->shader.fragment_shader_path);
  const std::string vertex_shader_binary_path = _ResolveShaderBinaryPath(out_request->vertex_shader_path);
  const std::string fragment_shader_binary_path = _ResolveShaderBinaryPath(out_request->fragment_shader_path);
  if (!_IsReadableNonEmptyFile(vertex_shader_binary_path)) {
    if (out_error_message) {
      *out_error_message = "Preview vertex shader binary is unavailable: " + vertex_shader_binary_path;
    }
    out_request->Clear();
    return false;
  }
  if (!_IsReadableNonEmptyFile(fragment_shader_binary_path)) {
    if (out_error_message) {
      *out_error_message = "Preview fragment shader binary is unavailable: " + fragment_shader_binary_path;
    }
    out_request->Clear();
    return false;
  }
  out_request->storage_buffers.reserve(result_stage->used_algorithm_containers.size());

  uint32_t instance_count = 0u;
  bool have_instance_count = false;
  for (const agent::AlgorithmInterventionContainerBinding& binding : result_stage->used_algorithm_containers) {
    const AlgorithmContainer* container = FindAlgorithmContainer(*container_set, binding.container_name);
    if (!container) {
      DEBUG_TOOL_ASSERT(false, "Required preview container is missing.");
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Missing preview container: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }
    const agent::AlgorithmReflectionValue* reflected_value = nullptr;
    if (reflection_snapshot) {
      reflected_value = _FindReflectionValue(*reflection_snapshot, binding.container_name);
    }

    const bool has_reflected_bytes = reflected_value && !reflected_value->bytes.empty();
    const bool has_container_bytes = !container->bytes.empty();
    if (container->element_stride == 0u || (!has_reflected_bytes && !has_container_bytes)) {
      DEBUG_TOOL_ASSERT(false, "Preview container has no drawable data.");
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Preview container has no data: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }

    runtime_systems::RenderPreviewBuffer preview_buffer{};
    preview_buffer.binding_name = binding.container_name;
    preview_buffer.element_stride = container->element_stride;
    if (has_reflected_bytes && reflected_value->storage_kind == container->storage_kind) {
      preview_buffer.bytes = reflected_value->bytes;
    } else {
      preview_buffer.bytes.assign(container->bytes.begin(), container->bytes.end());
    }
    out_request->storage_buffers.push_back(std::move(preview_buffer));

    const uint32_t buffer_instances = static_cast<uint32_t>(
      out_request->storage_buffers.back().bytes.size() / out_request->storage_buffers.back().element_stride);
    if (!have_instance_count) {
      instance_count = buffer_instances;
      have_instance_count = true;
    } else {
      instance_count = std::min(instance_count, buffer_instances);
    }
  }

  if (out_request->storage_buffers.empty()) {
    DEBUG_TOOL_ASSERT(false, "Result-render stage did not expose any usable array container.");
    if (out_error_message) {
      *out_error_message = "Result-render stage does not expose any usable array container.";
    }
    out_request->Clear();
    return false;
  }

  out_request->instance_count = instance_count;
  out_request->valid = out_request->instance_count > 0u;
  if (!out_request->valid) {
    DEBUG_TOOL_ASSERT(false, "Preview request has no drawable instances.");
    if (out_error_message) {
      *out_error_message = "Preview request has no drawable instances.";
    }
  }
  if (out_request->valid) {
    DEBUG_TOOL_ASSERT(
      !out_request->storage_buffers.empty(),
      "Preview request must contain at least one storage buffer.");
    DEBUG_TOOL_ASSERT(
      !out_request->stage_name.empty(),
      "Preview request must contain a stage name.");
  }
  return out_request->valid;
}

}  // namespace debug_tool_backend
