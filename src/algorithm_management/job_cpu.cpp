#include "algorithm_management/job_system.h"

#include "agent/agent_abi.h"

#include <string>

namespace algorithm_management::job_cpu {

bool Execute(
  const agent::AlgorithmObject& object,
  const agent::AgentTickContext& context,
  const AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "AlgorithmContainerSet pointer is null.";
    }
    return false;
  }
  if (!object.temporaryTest_main_thread_executor) {
    if (out_error_message) {
      *out_error_message = "CPU job executor is unavailable.";
    }
    return false;
  }
  return object.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
    context,
    object.algorithm_profile,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state);
}

}  // namespace algorithm_management::job_cpu
