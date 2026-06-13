#pragma once

// Internal algorithm-management job wrappers.
// Code outside src/algorithm_management should include
// algorithm_management/algorithm_manager.h instead of this file directly.

#if !defined(ALGORITHM_MANAGEMENT_LAYER_INTERNAL_BUILD) && !defined(ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include algorithm_management/job_system.h directly. Use algorithm_management/algorithm_manager.h."
#endif

#include "algorithm_management/algorithm_abi.h"

#include <string>

namespace algorithm_management::job_gpu {

bool Execute(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message = nullptr);

bool Synchronize(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

}  // namespace algorithm_management::job_gpu

namespace algorithm_management::job_cpu {

bool Execute(
  const agent::AlgorithmObject& object,
  const agent::AgentTickContext& context,
  const AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

}  // namespace algorithm_management::job_cpu
