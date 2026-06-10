#include "algorithm_management/job_system.h"

#include "agent_management/agent_abi.h"

#include <string>

namespace algorithm_management::job_cpu {

bool Execute(
  const agent::AgentAlgorithmCodecGroup& group,
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
  if (!group.temporaryTest_main_thread_executor) {
    if (out_error_message) {
      *out_error_message = "CPU job executor is unavailable.";
    }
    return false;
  }
  return group.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
    context,
    group.algorithm_profile,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state);
}

}  // namespace algorithm_management::job_cpu
