#pragma once

#include "algorithm_support/algorithm_abi.h"
#include "algorithm_support/algorithm_package_location.h"

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/runtime_systems.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace algorithm_management {

inline void ClearAlgorithmScheduler();
inline void SetAlgorithmRuntimeShutdownHook();
inline void ClearAlgorithmExecutionCaches();
inline bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);
inline bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr);
inline bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object);
inline bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

inline void SetAlgorithmRuntimeShutdownHook() {
  runtime_systems::SetRuntimeShutdownCallback(&ClearAlgorithmScheduler);
  runtime_systems::SetRuntimeGpuCacheClearCallback(&ClearAlgorithmExecutionCaches);
}

inline void ClearAlgorithmExecutionCaches() {
  runtime_systems::ClearRuntimeGpuJobCaches();
}

namespace pipeline_scheduler_detail {

inline bool HasPipelineStageBufferSlot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& stage_buffer_slot_name) {
  if (stage_buffer_slot_name.empty()) {
    return false;
  }
  const algorithm::AlgorithmContainer* stage_buffer_container =
    algorithm::FindAlgorithmContainer(container_set, stage_buffer_slot_name);
  return stage_buffer_container != nullptr &&
    algorithm::IsStandardContainerSlotName(container_set, stage_buffer_slot_name);
}

inline bool CopyPipelineCircularLoopback(
  const algorithm::AlgorithmContainerSet& source_container_set,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  ::agent::AlgorithmObject* target_stage,
  std::string* out_error_message) {
  if (!target_stage || !target_stage->mutable_container_set()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback target container set is unavailable.";
    }
    return false;
  }
  algorithm::AlgorithmContainerSet* target_container_set = target_stage->mutable_container_set();
  if (!source_container_set.standard_layout.enabled() ||
      !target_container_set->standard_layout.enabled()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback requires standard container layouts.";
    }
    return false;
  }
  if (source_container_set.standard_layout.variable_count < shared_variable_count ||
      target_container_set->standard_layout.variable_count < shared_variable_count ||
      source_container_set.standard_layout.array_count < shared_array_count ||
      target_container_set->standard_layout.array_count < shared_array_count) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback shared prefix is out of range.";
    }
    return false;
  }
  for (uint32_t i = 0; i < shared_variable_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeVariableName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback is missing standard variable slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback variable slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }
  for (uint32_t i = 0; i < shared_array_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeArrayName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback is missing standard array slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback array slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool CopyWholeStandardContainerLayout(
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::string* out_error_message) {
  if (!target_container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper standard container target is unavailable.";
    }
    return false;
  }
  if (!source_container_set.standard_layout.enabled() ||
      !target_container_set->standard_layout.enabled()) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper standard container copy requires standard layouts.";
    }
    return false;
  }
  if (source_container_set.standard_layout.variable_count != target_container_set->standard_layout.variable_count ||
      source_container_set.standard_layout.array_count != target_container_set->standard_layout.array_count) {
    if (out_error_message) {
      *out_error_message = "Pipeline wrapper standard container layout count mismatch.";
    }
    return false;
  }
  for (uint32_t i = 0u; i < source_container_set.standard_layout.variable_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeVariableName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper copy is missing standard variable slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper variable slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }
  for (uint32_t i = 0u; i < source_container_set.standard_layout.array_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeArrayName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper copy is missing standard array slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Pipeline wrapper array slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool BuildPipelineStageContainerSets(
  const std::vector<::algorithm_management::AlgorithmObject>& algorithm_objects,
  size_t begin_index,
  size_t end_index,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* out_stage_container_sets,
  std::string* out_error_message) {
  if (!out_stage_container_sets) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage container set output pointer is null.";
    }
    return false;
  }
  out_stage_container_sets->clear();
  out_stage_container_sets->reserve(end_index > begin_index ? end_index - begin_index : 0u);
  for (size_t index = begin_index; index < end_index; ++index) {
    const ::algorithm_management::AlgorithmObject& object = algorithm_objects[index];
    if (object.algorithm_profile.algorithm_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is empty.";
      }
      return false;
    }
    if (!object.container_set()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage container set is unavailable for '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
    const auto [_, inserted] = out_stage_container_sets->emplace(
      object.algorithm_profile.algorithm_name,
      object.shared_container_set);
    if (!inserted) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is duplicated: '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace pipeline_scheduler_detail

inline bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "CPU algorithm execution output pointer is null.";
    }
    return false;
  }
  if (!object.cpu_executor) {
    if (out_error_message) {
      *out_error_message = "CPU algorithm executor is unavailable.";
    }
    return false;
  }

  return runtime_systems::SubmitBlockingJob(
    context.job_priority == AlgorithmJobPriority::High
      ? runtime_systems::RuntimeJobPriority::High
      : (context.job_priority == AlgorithmJobPriority::Normal
        ? runtime_systems::RuntimeJobPriority::Normal
        : runtime_systems::RuntimeJobPriority::Low),
    [
      &object,
      &context,
      &agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state](std::string* out_job_error_message) {
      const bool ok = object.cpu_executor->ExecuteCpuAlgorithm(
        context,
        object.algorithm_profile,
        agent_to_algorithm_signal,
        container_set,
        out_algorithm_to_agent_signal,
        out_debug_state);
      if (!ok && out_job_error_message) {
        *out_job_error_message = "CPU algorithm execution failed.";
      }
    },
    out_error_message);
}

inline bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message) {
  return runtime_systems::ExecuteRuntimeGpuAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

inline bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  return runtime_systems::HasExecutableRuntimeGpuAlgorithmStage(object);
}

inline bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return runtime_systems::SynchronizeRuntimeGpuAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

inline std::string _ResolveAlgorithmGpuShaderPath(
  const ::agent::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message) {
  if (shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU shader path must not be empty.";
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
        "Failed to resolve GPU shader path for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return {};
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return resolved_path.string();
}

inline bool _TryBuildAlgorithmInterventionGpuStageSubJob(
  const ::agent::AlgorithmObject& object,
  const ::algorithm_management::AlgorithmInterventionStageSpec& stage_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeGpuStageSubJob* out_stage_job,
  std::string* out_error_message) {
  if (!container_set || !out_stage_job) {
    if (out_error_message) {
      *out_error_message = "GPU stage sub-job output pointer is null.";
    }
    return false;
  }
  if (stage_spec.shader.vertex_shader_path.empty() || stage_spec.shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU stage is missing shader paths.";
    }
    return false;
  }
  if (stage_spec.used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU stage does not bind any containers.";
    }
    return false;
  }

  out_stage_job->debug_name = object.algorithm_profile.algorithm_name + "::" + stage_spec.stage_name;
  out_stage_job->stage_name = stage_spec.stage_name;
  out_stage_job->vertex_shader_path = _ResolveAlgorithmGpuShaderPath(
    object,
    stage_spec.shader.vertex_shader_path,
    out_error_message);
  if (out_stage_job->vertex_shader_path.empty()) {
    return false;
  }
  out_stage_job->fragment_shader_path = _ResolveAlgorithmGpuShaderPath(
    object,
    stage_spec.shader.fragment_shader_path,
    out_error_message);
  if (out_stage_job->fragment_shader_path.empty()) {
    return false;
  }

  out_stage_job->buffer_bindings.reserve(stage_spec.used_algorithm_containers.size());
  for (const ::algorithm_management::AlgorithmInterventionContainerBinding& binding : stage_spec.used_algorithm_containers) {
    ::algorithm::AlgorithmContainer* container =
      ::algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU stage is missing container '" + binding.container_name + "'.")
          : ("GPU optional container '" + binding.container_name +
              "' is not supported because runtime GPU bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU stage container '" + binding.container_name + "' has no data.")
          : ("GPU optional container '" + binding.container_name +
              "' has no data, and sparse GPU bindings are not supported.");
      }
      return false;
    }

    runtime_systems::RuntimeGpuBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = binding.container_kind == "array";
    binding_view.required = binding.required;
    out_stage_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool _TryBuildAlgorithmGpuExecStageSubJob(
  const ::agent::AlgorithmObject& object,
  const ::algorithm_management::AlgorithmGpuExecSpec& gpu_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeGpuStageSubJob* out_stage_job,
  std::string* out_error_message) {
  if (!container_set || !out_stage_job) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage sub-job output pointer is null.";
    }
    return false;
  }
  if (gpu_exec_spec.shader.vertex_shader_path.empty() || gpu_exec_spec.shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage is missing shader paths.";
    }
    return false;
  }
  if (gpu_exec_spec.used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage does not bind any containers.";
    }
    return false;
  }

  out_stage_job->debug_name = object.algorithm_profile.algorithm_name + "::" + gpu_exec_spec.stage_name;
  out_stage_job->stage_name = gpu_exec_spec.stage_name;
  out_stage_job->vertex_shader_path = _ResolveAlgorithmGpuShaderPath(
    object,
    gpu_exec_spec.shader.vertex_shader_path,
    out_error_message);
  if (out_stage_job->vertex_shader_path.empty()) {
    return false;
  }
  out_stage_job->fragment_shader_path = _ResolveAlgorithmGpuShaderPath(
    object,
    gpu_exec_spec.shader.fragment_shader_path,
    out_error_message);
  if (out_stage_job->fragment_shader_path.empty()) {
    return false;
  }

  out_stage_job->buffer_bindings.reserve(gpu_exec_spec.used_algorithm_containers.size());
  for (const ::algorithm_management::AlgorithmGpuExecContainerBinding& binding : gpu_exec_spec.used_algorithm_containers) {
    ::algorithm::AlgorithmContainer* container =
      ::algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU exec stage is missing container '" + binding.container_name + "'.")
          : ("GPU exec optional container '" + binding.container_name +
              "' is not supported because runtime GPU bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU exec stage container '" + binding.container_name + "' has no data.")
          : ("GPU exec optional container '" + binding.container_name +
              "' has no data, and sparse GPU bindings are not supported.");
      }
      return false;
    }

    runtime_systems::RuntimeGpuBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = binding.container_kind == "array";
    binding_view.required = binding.required;
    out_stage_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline bool ExecuteAlgorithmObjectStagePlan(
  const ::agent::AlgorithmObject& object,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Algorithm execution output pointer is null.";
    }
    return false;
  }

  struct StageEntry {
    ::algorithm_management::AlgorithmExecutionPhase execution_phase{::algorithm_management::AlgorithmExecutionPhase::Exec};
    ::algorithm_management::AlgorithmExecutionPreference execution_preference{::algorithm_management::AlgorithmExecutionPreference::Cpu};
    const ::algorithm_management::AlgorithmInterventionStageSpec* intervention_stage{nullptr};
    bool exec_stage{false};
    bool reflect_stage{false};
  };

  std::vector<::algorithm_management::AlgorithmInterventionStageSpec> intervention_stage_specs{};
  if (object.intervention &&
      !object.intervention->GetInterventionStageSpecs(&intervention_stage_specs)) {
    if (out_error_message) {
      *out_error_message = "Algorithm intervention stage specifications are unavailable.";
    }
    return false;
  }

  const ::algorithm_management::AlgorithmInterventionStageSpec* pretick_stage{nullptr};
  const ::algorithm_management::AlgorithmInterventionStageSpec* aftertick_stage{nullptr};
  const ::algorithm_management::AlgorithmInterventionStageSpec* renderresult_stage{nullptr};
  for (const ::algorithm_management::AlgorithmInterventionStageSpec& stage_spec : intervention_stage_specs) {
    switch (stage_spec.stage_kind) {
      case ::algorithm_management::AlgorithmInterventionStageKind::Pretick:
        pretick_stage = &stage_spec;
        break;
      case ::algorithm_management::AlgorithmInterventionStageKind::AfterTick:
        aftertick_stage = &stage_spec;
        break;
      case ::algorithm_management::AlgorithmInterventionStageKind::ResultRender:
        renderresult_stage = &stage_spec;
        break;
      case ::algorithm_management::AlgorithmInterventionStageKind::Exec:
      case ::algorithm_management::AlgorithmInterventionStageKind::Reflect:
      case ::algorithm_management::AlgorithmInterventionStageKind::Custom:
        break;
    }
  }

  std::vector<StageEntry> stage_entries{};
  if (pretick_stage) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algorithm_management::AlgorithmExecutionPhase::Pretick,
      .execution_preference = pretick_stage->execution_preference,
      .intervention_stage = pretick_stage,
    });
  }
  stage_entries.push_back(StageEntry{
    .execution_phase = ::algorithm_management::AlgorithmExecutionPhase::Exec,
    .execution_preference = object.execution_preference,
    .exec_stage = true,
  });
  if (aftertick_stage) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algorithm_management::AlgorithmExecutionPhase::AfterTick,
      .execution_preference = aftertick_stage->execution_preference,
      .intervention_stage = aftertick_stage,
    });
  }
  if (renderresult_stage) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algorithm_management::AlgorithmExecutionPhase::RenderResult,
      .execution_preference = renderresult_stage->execution_preference,
      .intervention_stage = renderresult_stage,
    });
  }
  if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algorithm_management::AlgorithmExecutionPhase::Reflect,
      .execution_preference = ::algorithm_management::AlgorithmExecutionPreference::Cpu,
      .reflect_stage = true,
    });
  }

  for (size_t stage_offset = 0u; stage_offset < stage_entries.size(); ) {
    const ::algorithm_management::AlgorithmExecutionPreference bundle_preference =
      stage_entries[stage_offset].execution_preference;
    const size_t bundle_begin = stage_offset;
    size_t bundle_end = stage_offset + 1u;
    while (bundle_end < stage_entries.size() &&
           stage_entries[bundle_end].execution_preference == bundle_preference) {
      ++bundle_end;
    }

    if (bundle_preference == ::algorithm_management::AlgorithmExecutionPreference::Gpu) {
      runtime_systems::RuntimeGpuStageJob gpu_job{};
      gpu_job.shader_namespace = object.algorithm_profile.algorithm_name;
      gpu_job.execution_key = container_set;
      gpu_job.viewport_width = std::max(context.render_preview_extent.x, 1.0f);
      gpu_job.viewport_height = std::max(context.render_preview_extent.y, 1.0f);
      for (size_t index = bundle_begin; index < bundle_end; ++index) {
        const StageEntry& entry = stage_entries[index];
        runtime_systems::RuntimeGpuStageSubJob stage_job{};
        if (entry.exec_stage) {
          if (!object.gpu_executor) {
            if (out_error_message) {
              *out_error_message = "GPU exec stage is unavailable.";
            }
            return false;
          }
          ::algorithm_management::AlgorithmGpuExecSpec gpu_exec_spec{};
          if (!object.gpu_executor->GetGpuExecSpec(&gpu_exec_spec)) {
            if (out_error_message) {
              *out_error_message = "GPU exec stage failed to provide its execution spec.";
            }
            return false;
          }
          if (!_TryBuildAlgorithmGpuExecStageSubJob(
                object,
                gpu_exec_spec,
                container_set,
                &stage_job,
                out_error_message)) {
            return false;
          }
        } else if (entry.intervention_stage) {
          if (!_TryBuildAlgorithmInterventionGpuStageSubJob(
                object,
                *entry.intervention_stage,
                container_set,
                &stage_job,
                out_error_message)) {
            return false;
          }
        } else {
          if (out_error_message) {
            *out_error_message = "GPU bundle contains an unsupported stage.";
          }
          return false;
        }
        gpu_job.stage_jobs.push_back(std::move(stage_job));
      }
      if (gpu_job.stage_jobs.empty()) {
        if (out_error_message) {
          *out_error_message = "GPU bundle does not contain any executable stages.";
        }
        return false;
      }
      gpu_job.debug_name = gpu_job.stage_jobs.front().debug_name;
      gpu_job.stage_name = gpu_job.stage_jobs.front().stage_name;
      gpu_job.vertex_shader_path = gpu_job.stage_jobs.front().vertex_shader_path;
      gpu_job.fragment_shader_path = gpu_job.stage_jobs.front().fragment_shader_path;
      gpu_job.buffer_bindings = gpu_job.stage_jobs.front().buffer_bindings;
      if (!runtime_systems::ExecuteRuntimeGpuJob(gpu_job, out_error_message)) {
        return false;
      }
      if (!runtime_systems::SynchronizeRuntimeGpuJob(gpu_job, out_error_message)) {
        return false;
      }
    } else {
      for (size_t index = bundle_begin; index < bundle_end; ++index) {
        const StageEntry& entry = stage_entries[index];
        if (entry.reflect_stage) {
          continue;
        }

        ::agent::AgentTickContext stage_context = context;
        stage_context.execution_phase = entry.execution_phase;
        if (!ExecuteCpuAlgorithmObject(
              object,
              stage_context,
              agent_to_algorithm_signal,
              container_set,
              out_algorithm_to_agent_signal,
              out_debug_state,
              out_error_message)) {
          return false;
        }
      }
    }

    stage_offset = bundle_end;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_management
