#pragma once

#include "algomanager/bridge/algorithm_protocol.h"

#include <string>

namespace runtimesys {
struct RuntimeVkStageSubJob;
}

namespace algomanager { namespace bridge { namespace runtime_bridge_support {

std::string ResolveAlgorithmVkShaderPath(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message);

bool TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

bool TryBuildAlgorithmVkExecStageSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

}  // namespace runtime_bridge_support
}  // namespace bridge
}  // namespace algomanager
