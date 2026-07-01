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
  return runtime_systems::ExecuteRuntimeGpuAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  return runtime_systems::HasExecutableRuntimeGpuAlgorithmStage(object);
}

bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return runtime_systems::SynchronizeRuntimeGpuAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

}  // namespace scheduler
}  // namespace algorithmManager
