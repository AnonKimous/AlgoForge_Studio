#include "algorithm_management/job_system.h"

#include "algorithm_management/algorithm_abi.h"

#include <string>

namespace runtime_systems {

bool TryExecuteGpuTick(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message);

bool TrySynchronizeGpuTickState(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

}  // namespace runtime_systems

namespace algorithm_management::job_gpu {

bool Execute(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const agent::AgentTickContext& context,
  std::string* out_error_message) {
  return runtime_systems::TryExecuteGpuTick(object, container_set, context, out_error_message);
}

bool Synchronize(
  const agent::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return runtime_systems::TrySynchronizeGpuTickState(object, container_set, out_error_message);
}

}  // namespace algorithm_management::job_gpu
