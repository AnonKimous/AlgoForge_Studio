#pragma once

#include "debug_tool/agent_hooker.h"
#include "debug_tool/agent_management_hooker.h"
#include "debug_tool/algorithm_manager_hooker.h"
#include "debug_tool/runtime_systems_hooker.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <system_error>
#include <string>
#include <utility>
#include <vector>

namespace debug_tool_backend::hooker {

using AgentManagementHooker = agent_management_hooker::AgentManagementHooker;
using RuntimeSystemsHooker = runtime_systems_hooker::RuntimeSystemsHooker;
using RuntimeEnvironment = runtime_systems_hooker::RuntimeEnvironment;
using RenderPreviewRequest = runtime_systems_hooker::RenderPreviewRequest;
using RenderPreviewBuffer = runtime_systems_hooker::RenderPreviewBuffer;

using agent_hooker::AlgorithmCount;
using agent_hooker::AlgorithmObjectAt;
using agent_hooker::AlgorithmRuntimeStateAt;
using agent_hooker::AgentName;
using agent_hooker::BeginAlgorithmAssembly;
using agent_hooker::ContainerSet;

inline std::string AlgorithmCatalogPath() {
  return algorithm_manager_hooker::AlgorithmCatalogPath();
}

inline std::string ProjectRootPath() {
  return algorithm_manager_hooker::ProjectRootPath();
}

inline std::filesystem::path ResolveAlgorithmLibrarySourceRoot() {
  return algorithm_manager_hooker::ResolveAlgorithmLibrarySourceRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimeRoot() {
  return algorithm_manager_hooker::ResolveAlgorithmLibraryRuntimeRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() {
  return algorithm_manager_hooker::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot();
}

inline std::string HotReloadBuildCommand(const std::string& algorithm_name) {
  return algorithm_manager_hooker::HotReloadBuildCommand(algorithm_name);
}

inline bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  ::algorithm::AlgorithmPackageLocation* out_location,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
    algorithm_name,
    out_location,
    out_error_message);
}

inline bool LoadAlgorithmPackageTransferMapFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map = nullptr,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::LoadAlgorithmPackageTransferMapFromLocation(
    package_location,
    out_transfer_map,
    out_has_transfer_map,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algorithm_management::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithm_management::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<algorithm_management::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithm_management::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::LoadAlgorithmPackageDefaultBindings(
    algorithm_name,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  algorithm_management::AlgorithmRequestedResources* out_resources,
  algorithm_management::AlgorithmRequestedDescriptorBindings* out_descriptors,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::QueryAlgorithmRequestedBindings(
    algorithm_name,
    out_resources,
    out_descriptors,
    out_error_message);
}

inline bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& agent_name,
  algorithm_management::CpuPipelineRuntimeState* out_runtime_state) {
  return algorithm_manager_hooker::TryGetMountedPipelineRuntime(
    pipeline_name,
    agent_name,
    out_runtime_state);
}

inline bool TryGetMountedPipelineRegistration(
  const std::string& pipeline_name,
  algorithm_management::CpuPipelineRegistration* out_registration) {
  return algorithm_manager_hooker::TryGetMountedPipelineRegistration(
    pipeline_name,
    out_registration);
}

inline void ClearAlgorithmScheduler() {
  algorithm_manager_hooker::ClearAlgorithmScheduler();
}

inline void SetAlgorithmRuntimeShutdownHook() {
  algorithm_manager_hooker::SetAlgorithmRuntimeShutdownHook();
}

inline void ClearAlgorithmExecutionCaches() {
  algorithm_manager_hooker::ClearAlgorithmExecutionCaches();
}

inline bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::ExecuteCpuAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::ExecuteGpuAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

inline bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  return algorithm_manager_hooker::HasExecutableGpuAlgorithmStage(object);
}

inline bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return algorithm_manager_hooker::SynchronizeGpuAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

inline bool ShouldEmitPipelineRunnerProbe(const std::string& pipeline_name) {
  return pipeline_name.find("::runner_mount") != std::string::npos;
}

inline void AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line) {
  const std::filesystem::path path =
    ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() / file_name;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

inline std::string ResolveAlgorithmShaderPath(
  const ::agent::AlgorithmObject& object,
  const std::string& shader_path) {
  if (shader_path.empty()) {
    return {};
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    return path.string();
  }

  std::filesystem::path runtime_package_root(object.runtime_package_root_path);
  if (runtime_package_root.empty()) {
    ::algorithm::AlgorithmPackageLocation package_location{};
    std::string error_message;
    const bool resolved = algorithm_manager_hooker::TryResolveAlgorithmPackageLocation(
      object.algorithm_profile.algorithm_name,
      &package_location,
      &error_message);
    assert(resolved && "Failed to resolve algorithm package location for shader path.");
    if (!resolved) {
      return {};
    }
    runtime_package_root = package_location.runtime_package_root;
  }
  if (runtime_package_root.empty()) {
    assert(false && "Runtime package root is empty for shader path.");
    return {};
  }

  return (runtime_package_root / path).lexically_normal().string();
}

inline std::string ResolveShaderBinaryPath(const std::string& shader_path) {
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

inline bool IsReadableNonEmptyFile(const std::string& path) {
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

inline const agent::AlgorithmReflectionValue* FindReflectionValue(
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

inline bool TryFindPipelineGroupRange(
  const agent::Agent& managed_agent,
  size_t anchor_index,
  size_t* out_begin_index,
  size_t* out_end_index) {
  if (!out_begin_index || !out_end_index || anchor_index >= AlgorithmCount(managed_agent)) {
    return false;
  }

  const agent::AlgorithmObject* anchor = AlgorithmObjectAt(managed_agent, anchor_index);
  if (!anchor || !anchor->pipeline_stage || anchor->pipeline_name.empty()) {
    return false;
  }

  size_t begin_index = anchor_index;
  while (begin_index > 0u) {
    const agent::AlgorithmObject* previous = AlgorithmObjectAt(managed_agent, begin_index - 1u);
    const agent::AlgorithmObject* current = AlgorithmObjectAt(managed_agent, begin_index);
    if (!previous ||
        !current ||
        !previous->pipeline_stage ||
        previous->pipeline_name != anchor->pipeline_name ||
        previous->pipeline_stage_index + 1u != current->pipeline_stage_index) {
      break;
    }
    --begin_index;
  }

  size_t end_index = anchor_index + 1u;
  while (end_index < AlgorithmCount(managed_agent)) {
    const agent::AlgorithmObject* previous = AlgorithmObjectAt(managed_agent, end_index - 1u);
    const agent::AlgorithmObject* next = AlgorithmObjectAt(managed_agent, end_index);
    if (!previous ||
        !next ||
        !next->pipeline_stage ||
        next->pipeline_name != anchor->pipeline_name ||
        previous->pipeline_stage_index + 1u != next->pipeline_stage_index) {
      break;
    }
    ++end_index;
  }

  *out_begin_index = begin_index;
  *out_end_index = end_index;
  return true;
}

inline bool TryLoadInterventionStageSpecs(
  const agent::AlgorithmObject& object,
  std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs) {
  if (!out_stage_specs) {
    return false;
  }
  out_stage_specs->clear();
  if (!object.intervention) {
    return true;
  }
  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!object.intervention->GetInterventionStageSpecs(&stage_specs) || stage_specs.empty()) {
    return true;
  }
  *out_stage_specs = std::move(stage_specs);
  return true;
}

inline bool ContainsResultRenderStage(const std::vector<agent::AlgorithmInterventionStageSpec>& stage_specs) {
  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      return true;
    }
  }
  return false;
}

inline void AppendUniquePipelineStageIndex(
  size_t candidate_index,
  std::unordered_set<size_t>* seen_indices,
  std::vector<size_t>* ordered_indices) {
  if (seen_indices->insert(candidate_index).second) {
    ordered_indices->push_back(candidate_index);
  }
}

inline void AppendPipelineSummaryInterventionStages(
  const agent::Agent& managed_agent,
  size_t stage_index,
  const algorithm_management::CpuPipelineRegistration& registration,
  std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs) {
  size_t pipeline_begin_index = 0u;
  size_t pipeline_end_index = 0u;
  if (!TryFindPipelineGroupRange(managed_agent, stage_index, &pipeline_begin_index, &pipeline_end_index)) {
    return;
  }

  const size_t body_begin_index =
    pipeline_begin_index + static_cast<size_t>(registration.body_begin_stage_index);
  const size_t body_stage_count = static_cast<size_t>(registration.body_stage_count);
  const size_t body_end_index = body_begin_index + body_stage_count;
  const size_t effective_tail_index =
    pipeline_begin_index + static_cast<size_t>(registration.effective_tail_stage_index);

  if (static_cast<size_t>(registration.body_begin_stage_index) > 0u && pipeline_begin_index < pipeline_end_index) {
    const agent::AlgorithmObject* wrapper_begin = AlgorithmObjectAt(managed_agent, pipeline_begin_index);
    if (wrapper_begin) {
      std::vector<agent::AlgorithmInterventionStageSpec> wrapper_begin_specs;
      if (TryLoadInterventionStageSpecs(*wrapper_begin, &wrapper_begin_specs) &&
          !wrapper_begin_specs.empty()) {
        out_stage_specs->insert(
          out_stage_specs->end(),
          wrapper_begin_specs.begin(),
          wrapper_begin_specs.end());
      }
    }
  }

  const bool has_wrapper_end =
    effective_tail_index >= body_end_index &&
    effective_tail_index < pipeline_end_index;
  if (has_wrapper_end) {
    const agent::AlgorithmObject* wrapper_end = AlgorithmObjectAt(managed_agent, effective_tail_index);
    if (wrapper_end) {
      std::vector<agent::AlgorithmInterventionStageSpec> wrapper_end_specs;
      if (TryLoadInterventionStageSpecs(*wrapper_end, &wrapper_end_specs) &&
          !wrapper_end_specs.empty()) {
        out_stage_specs->insert(
          out_stage_specs->end(),
          wrapper_end_specs.begin(),
          wrapper_end_specs.end());
      }
    }
  }
}

inline bool TryResolveRenderPreviewSource(
  const agent::Agent& managed_agent,
  size_t selected_index,
  size_t* out_source_index,
  std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs,
  std::string* out_error_message) {
  if (!out_source_index || !out_stage_specs) {
    if (out_error_message) {
      *out_error_message = "Preview source output pointer is null.";
    }
    return false;
  }

  *out_source_index = selected_index;
  out_stage_specs->clear();
  const agent::AlgorithmObject* selected_object = AlgorithmObjectAt(managed_agent, selected_index);
  if (!selected_object) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm object is unavailable.";
    }
    return false;
  }

  if (!selected_object->pipeline_stage || selected_object->pipeline_name.empty()) {
    if (!TryLoadInterventionStageSpecs(*selected_object, out_stage_specs) || out_stage_specs->empty()) {
      if (out_error_message) {
        *out_error_message = "Algorithm intervention did not expose any stages.";
      }
      return false;
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  size_t pipeline_begin_index = 0u;
  size_t pipeline_end_index = 0u;
  if (!TryFindPipelineGroupRange(managed_agent, selected_index, &pipeline_begin_index, &pipeline_end_index)) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline stage range is unavailable.";
    }
    return false;
  }

  algorithm_management::CpuPipelineRegistration registration{};
  const bool has_registration = TryGetMountedPipelineRegistration(selected_object->pipeline_name, &registration);
  std::unordered_set<size_t> seen_indices{};
  std::vector<size_t> candidate_indices{};
  if (has_registration) {
    const size_t effective_tail_index =
      pipeline_begin_index + static_cast<size_t>(registration.effective_tail_stage_index);
    const size_t body_begin_index =
      pipeline_begin_index + static_cast<size_t>(registration.body_begin_stage_index);
    const size_t body_stage_count = static_cast<size_t>(registration.body_stage_count);
    const size_t body_end_index = body_begin_index + body_stage_count;

    if (selected_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(selected_index, &seen_indices, &candidate_indices);
    }
    if (effective_tail_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(effective_tail_index, &seen_indices, &candidate_indices);
    }
    if (registration.body_begin_stage_index > 0u && pipeline_begin_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(pipeline_begin_index, &seen_indices, &candidate_indices);
    }
    if (body_begin_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(body_begin_index, &seen_indices, &candidate_indices);
    }
    if (body_end_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(body_end_index, &seen_indices, &candidate_indices);
    }
    if (pipeline_begin_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(pipeline_end_index - 1u, &seen_indices, &candidate_indices);
    }
  } else {
    if (selected_index < pipeline_end_index) {
      AppendUniquePipelineStageIndex(selected_index, &seen_indices, &candidate_indices);
    }
  }

  if (candidate_indices.empty()) {
    if (selected_index < AlgorithmCount(managed_agent)) {
      AppendUniquePipelineStageIndex(selected_index, &seen_indices, &candidate_indices);
    }
  }

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs{};
  for (size_t candidate_index : candidate_indices) {
    const agent::AlgorithmObject* candidate = AlgorithmObjectAt(managed_agent, candidate_index);
    if (!candidate) {
      continue;
    }
    if (!TryLoadInterventionStageSpecs(*candidate, &stage_specs) || stage_specs.empty()) {
      continue;
    }
    if (ContainsResultRenderStage(stage_specs)) {
      *out_source_index = candidate_index;
      *out_stage_specs = stage_specs;
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
  }

  if (out_error_message) {
    *out_error_message = "Algorithm intervention did not expose a result-render stage.";
  }
  return false;
}

}  // namespace debug_tool_backend::hooker
