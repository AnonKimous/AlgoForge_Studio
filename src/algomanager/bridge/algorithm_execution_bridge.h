#pragma once

#include "algomanager/bridge/algorithm_abi.h"

#include <string>

namespace runtimesys {
struct RuntimeVkStageSubJob;
}

namespace algomanager { namespace bridge { namespace execution_bridge_support {

std::string ResolveAlgorithmVkShaderPath(
  const ::algomanager::bridge::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message);

bool TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

bool TryBuildAlgorithmVkExecStageSubJob(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

}  // namespace execution_bridge_support
}  // namespace bridge
}  // namespace algomanager
