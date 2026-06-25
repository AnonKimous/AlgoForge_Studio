#pragma once

#include "algorithm_support/algorithm_support.h"

namespace algorithmManager {
namespace scheduler {

void SetAlgorithmRuntimeShutdownHook();
void ClearAlgorithmExecutionCaches();
bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);
bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr);
bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object);
bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

}  // namespace scheduler
}  // namespace algorithmManager

namespace algorithm_management {

inline void SetAlgorithmRuntimeShutdownHook() {
  algorithmManager::scheduler::SetAlgorithmRuntimeShutdownHook();
}

inline void ClearAlgorithmExecutionCaches() {
  algorithmManager::scheduler::ClearAlgorithmExecutionCaches();
}

inline bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return algorithmManager::scheduler::ExecuteCpuAlgorithmObject(
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
  return algorithmManager::scheduler::ExecuteGpuAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

inline bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  return algorithmManager::scheduler::HasExecutableGpuAlgorithmStage(object);
}

inline bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return algorithmManager::scheduler::SynchronizeGpuAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

}  // namespace algorithm_management
