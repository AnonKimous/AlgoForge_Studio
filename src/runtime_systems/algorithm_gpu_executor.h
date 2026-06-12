#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD)
#error "Do not include runtime_systems/algorithm_gpu_executor.h directly. Use algorithm_management/algorithm_manager.h."
#endif

#include "agent/agent_abi.h"
#include "algorithm_management/algorithm_manager.h"

#include <string>

namespace runtime_systems {

class AlgorithmGpuExecutor {
 public:
  static AlgorithmGpuExecutor& Instance();

  void Clear();

  bool ExecuteGpuTick(
    const agent::AgentAlgorithmSupportGroup& group,
    algorithm::AlgorithmContainerSet* container_set,
    const agent::AgentTickContext& context,
    std::string* out_error_message);

  bool SynchronizeGpuTickState(
    const agent::AgentAlgorithmSupportGroup& group,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message);

 private:
  AlgorithmGpuExecutor() = default;
};

bool TryExecuteGpuTick(
  const agent::AgentAlgorithmSupportGroup& group,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message = nullptr);

bool TrySynchronizeGpuTickState(
  const agent::AgentAlgorithmSupportGroup& group,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

}  // namespace runtime_systems
