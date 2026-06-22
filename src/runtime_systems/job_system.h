#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/job_system.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include "algorithm_management/algorithm_abi.h"

#include <cstddef>
#include <string>

namespace runtime_systems {

bool InitializeJobSystem(size_t worker_count = 7u);
void ShutdownJobSystem();
bool IsJobSystemInitialized();

namespace job_cpu {

void Clear();

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  const algorithm_management::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  algorithm_management::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

}  // namespace job_cpu

namespace job_gpu {

void Clear();

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const algorithm_management::AgentTickContext& context,
  std::string* out_error_message = nullptr);

bool Synchronize(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

}  // namespace job_gpu

}  // namespace runtime_systems
