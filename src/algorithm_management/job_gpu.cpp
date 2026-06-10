#include "algorithm_management/job_system.h"

#include "agent_management/agent_abi.h"

#include <string>

namespace runtime_systems {

bool TryExecuteGpuTick(
  const agent::AgentAlgorithmCodecGroup& group,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message);

bool TrySynchronizeGpuTickState(
  const agent::AgentAlgorithmCodecGroup& group,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

}  // namespace runtime_systems

namespace algorithm_management::job_gpu {

bool Execute(
  const agent::AgentAlgorithmCodecGroup& group,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message) {
  return runtime_systems::TryExecuteGpuTick(group, container_set, context, out_error_message);
}

bool Synchronize(
  const agent::AgentAlgorithmCodecGroup& group,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return runtime_systems::TrySynchronizeGpuTickState(group, container_set, out_error_message);
}

}  // namespace algorithm_management::job_gpu
