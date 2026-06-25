#include "algorithm_support/algorithm_library_paths.h"
#include "algorithm_management/algorithm_scheduler_runtime_bridge.h"
#include "algorithm_management/algorithm_manager.h"
#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/runtime_systems.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace algorithmManager {
namespace scheduler {

namespace {

runtime_systems::RuntimeJobPriority _ToRuntimeJobPriority(AlgorithmJobPriority priority) {
  switch (priority) {
    case AlgorithmJobPriority::High:
      return runtime_systems::RuntimeJobPriority::High;
    case AlgorithmJobPriority::Normal:
      return runtime_systems::RuntimeJobPriority::Normal;
    case AlgorithmJobPriority::Low:
      return runtime_systems::RuntimeJobPriority::Low;
  }
  return runtime_systems::RuntimeJobPriority::High;
}

void _ClearAlgorithmSchedulerForRuntimeShutdown() {
  algorithm_management::ClearAlgorithmScheduler();
}

bool _IsGpuExecutionStage(const agent::AlgorithmInterventionStageSpec& stage) {
  return stage.stage_kind == agent::AlgorithmInterventionStageKind::PostExecution &&
    (stage.stage_name == "afterTick" || stage.stage_name == "aftertick");
}

bool _HasRuntimeGpuStagePayload(const agent::AlgorithmInterventionStageSpec& stage) {
  return !stage.shader.vertex_shader_path.empty() ||
    !stage.shader.fragment_shader_path.empty() ||
    !stage.used_algorithm_containers.empty();
}

bool _ResolveRuntimeGpuShaderPath(
  const std::string& algorithm_name,
  const std::string& shader_path,
  std::string* out_resolved_path,
  std::string* out_error_message) {
  if (!out_resolved_path) {
    if (out_error_message) {
      *out_error_message = "Resolved GPU shader path output pointer is null.";
    }
    return false;
  }
  if (shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU shader path must not be empty.";
    }
    return false;
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    *out_resolved_path = path.string();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string resolve_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &resolve_error_message)) {
    if (out_error_message) {
      *out_error_message = resolve_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(resolve_error_message);
    }
    return false;
  }
  if (package_location.runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm runtime package root is empty for '" + algorithm_name + "'.";
    }
    return false;
  }

  *out_resolved_path = (package_location.runtime_package_root / path).lexically_normal().string();
  if (out_resolved_path->empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve GPU shader path for '" + algorithm_name + "'.";
    }
    return false;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryBuildRuntimeGpuStageJob(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeGpuStageJob* out_job,
  bool* out_has_gpu_stage,
  std::string* out_error_message) {
  if (!container_set || !out_job || !out_has_gpu_stage) {
    if (out_error_message) {
      *out_error_message = "Runtime GPU stage job output pointer is null.";
    }
    return false;
  }

  *out_job = {};
  *out_has_gpu_stage = false;
  if (!object.intervention) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!object.intervention->GetInterventionStageSpecs(&stage_specs) || stage_specs.empty()) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  const agent::AlgorithmInterventionStageSpec* gpu_stage = nullptr;
  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (!_IsGpuExecutionStage(stage_spec)) {
      continue;
    }
    // Debug tooling may inject an empty afterTick placeholder. Skip it unless
    // the manifest actually provides runtime GPU shader/container payload.
    if (!_HasRuntimeGpuStagePayload(stage_spec)) {
      continue;
    }
    gpu_stage = &stage_spec;
    break;
  }
  if (!gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  *out_has_gpu_stage = true;
  if (gpu_stage->shader.vertex_shader_path.empty() || gpu_stage->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU tick stage is missing shader paths.";
    }
    return false;
  }
  if (gpu_stage->used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU tick stage does not bind any containers.";
    }
    return false;
  }

  out_job->debug_name = object.algorithm_profile.algorithm_name + "::" + gpu_stage->stage_name;
  out_job->shader_namespace = object.algorithm_profile.algorithm_name;
  out_job->stage_name = gpu_stage->stage_name;
  out_job->execution_key = container_set;
  if (!_ResolveRuntimeGpuShaderPath(
        object.algorithm_profile.algorithm_name,
        gpu_stage->shader.vertex_shader_path,
        &out_job->vertex_shader_path,
        out_error_message)) {
    return false;
  }
  if (!_ResolveRuntimeGpuShaderPath(
        object.algorithm_profile.algorithm_name,
        gpu_stage->shader.fragment_shader_path,
        &out_job->fragment_shader_path,
        out_error_message)) {
    return false;
  }

  out_job->buffer_bindings.reserve(gpu_stage->used_algorithm_containers.size());
  for (const agent::AlgorithmInterventionContainerBinding& binding : gpu_stage->used_algorithm_containers) {
    algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU tick stage is missing container '" + binding.container_name + "'.")
          : ("GPU tick optional container '" + binding.container_name +
              "' is not supported because runtime GPU bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU tick stage container '" + binding.container_name + "' has no data.")
          : ("GPU tick optional container '" + binding.container_name +
              "' has no data, and sparse GPU bindings are not supported.");
      }
      return false;
    }

    runtime_systems::RuntimeGpuBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.required = binding.required;
    out_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

void SetAlgorithmRuntimeShutdownHook() {
  runtime_systems::SetRuntimeShutdownCallback(&_ClearAlgorithmSchedulerForRuntimeShutdown);
  runtime_systems::SetRuntimeGpuCacheClearCallback(&ClearAlgorithmExecutionCaches);
}

void ClearAlgorithmExecutionCaches() {
  runtime_systems::ClearRuntimeGpuJobCaches();
}

bool ExecuteCpuAlgorithmObject(
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
    _ToRuntimeJobPriority(context.job_priority),
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

bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message) {
  (void)context;
  runtime_systems::RuntimeGpuStageJob job{};
  bool has_gpu_stage = false;
  if (!_TryBuildRuntimeGpuStageJob(
        object,
        container_set,
        &job,
        &has_gpu_stage,
        out_error_message)) {
    return false;
  }
  if (!has_gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  return runtime_systems::ExecuteRuntimeGpuJob(job, out_error_message);
}

bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  if (!object.intervention) {
    return false;
  }

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!object.intervention->GetInterventionStageSpecs(&stage_specs)) {
    return false;
  }

  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (_IsGpuExecutionStage(stage_spec) && _HasRuntimeGpuStagePayload(stage_spec)) {
      return true;
    }
  }
  return false;
}

bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  runtime_systems::RuntimeGpuStageJob job{};
  bool has_gpu_stage = false;
  if (!_TryBuildRuntimeGpuStageJob(
        object,
        container_set,
        &job,
        &has_gpu_stage,
        out_error_message)) {
    return false;
  }
  if (!has_gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  return runtime_systems::SynchronizeRuntimeGpuJob(job, out_error_message);
}

}  // namespace scheduler
}  // namespace algorithmManager
