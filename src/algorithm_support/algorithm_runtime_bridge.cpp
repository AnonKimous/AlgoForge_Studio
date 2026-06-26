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

bool _ResolveRuntimeGpuShaderPath(
  const ::agent::AlgorithmObject& object,
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

  std::filesystem::path runtime_package_root(object.runtime_package_root_path);
  if (runtime_package_root.empty()) {
    ::algorithm::AlgorithmPackageLocation package_location{};
    std::string resolve_error_message;
    if (!TryResolveAlgorithmPackageLocation(
          object.algorithm_profile.algorithm_name,
          &package_location,
          &resolve_error_message)) {
      if (out_error_message) {
        *out_error_message = resolve_error_message.empty()
          ? ("Failed to resolve algorithm package location for '" + object.algorithm_profile.algorithm_name + "'.")
          : std::move(resolve_error_message);
      }
      return false;
    }
    runtime_package_root = package_location.runtime_package_root;
  }
  if (runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Algorithm runtime package root is empty for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return false;
  }

  *out_resolved_path = (runtime_package_root / path).lexically_normal().string();
  if (out_resolved_path->empty()) {
    if (out_error_message) {
      *out_error_message =
        "Failed to resolve GPU shader path for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return false;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryLoadRuntimeGpuExecSpec(
  const ::agent::AlgorithmObject& object,
  agent::AlgorithmGpuExecSpec* out_spec,
  bool* out_has_gpu_exec,
  std::string* out_error_message) {
  if (!out_spec || !out_has_gpu_exec) {
    if (out_error_message) {
      *out_error_message = "Runtime GPU exec spec output pointer is null.";
    }
    return false;
  }

  *out_spec = {};
  *out_has_gpu_exec = false;
  if (!object.gpu_executor) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!object.gpu_executor->GetGpuExecSpec(out_spec)) {
    if (out_error_message) {
      *out_error_message = "GPU executor failed to provide its exec specification.";
    }
    return false;
  }
  if (out_spec->shader.vertex_shader_path.empty() ||
      out_spec->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage is missing shader paths.";
    }
    return false;
  }
  if (out_spec->used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage does not bind any containers.";
    }
    return false;
  }
  if (out_spec->stage_name.empty()) {
    out_spec->stage_name = "exec";
  }
  *out_has_gpu_exec = true;
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
  agent::AlgorithmGpuExecSpec gpu_exec_spec{};
  if (!_TryLoadRuntimeGpuExecSpec(
        object,
        &gpu_exec_spec,
        out_has_gpu_stage,
        out_error_message)) {
    return false;
  }
  if (!*out_has_gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_job->debug_name = object.algorithm_profile.algorithm_name + "::" + gpu_exec_spec.stage_name;
  out_job->shader_namespace = object.algorithm_profile.algorithm_name;
  out_job->stage_name = gpu_exec_spec.stage_name;
  out_job->execution_key = container_set;
  if (!_ResolveRuntimeGpuShaderPath(
        object,
        gpu_exec_spec.shader.vertex_shader_path,
        &out_job->vertex_shader_path,
        out_error_message)) {
    return false;
  }
  if (!_ResolveRuntimeGpuShaderPath(
        object,
        gpu_exec_spec.shader.fragment_shader_path,
        &out_job->fragment_shader_path,
        out_error_message)) {
    return false;
  }

  out_job->buffer_bindings.reserve(gpu_exec_spec.used_algorithm_containers.size());
  for (const agent::AlgorithmGpuExecContainerBinding& binding : gpu_exec_spec.used_algorithm_containers) {
    algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.container_name);
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
  agent::AlgorithmGpuExecSpec gpu_exec_spec{};
  bool has_gpu_exec = false;
  return _TryLoadRuntimeGpuExecSpec(object, &gpu_exec_spec, &has_gpu_exec, nullptr) &&
    has_gpu_exec;
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
