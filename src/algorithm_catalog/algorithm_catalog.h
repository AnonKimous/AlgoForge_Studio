#pragma once

#include "algorithm_catalog/algorithm_abi.h"
#include "algorithm_catalog/algorithm_container_manifest.h"
#include "algorithm_catalog/algorithm_data.h"
#include "algorithm_catalog/algorithm_interaction_protocol.h"
#include "algorithm_catalog/algorithm_intervention_support.h"
#include "algorithm_catalog/algorithm_package_location.h"
#include "algorithm_catalog/algorithm_protocol.h"
#include "algorithm_catalog/algorithm_types.h"
#include "algorithm_catalog/algorithm_library_paths.h"

namespace algorithmManager {
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmReflectorManifestItem;
using ::algorithm::AlgorithmPackageLocation;
}

namespace runtime_systems {
struct RuntimeVkStageSubJob;
}

namespace algorithmManager { namespace catalog { namespace runtime_bridge_support {

std::string ResolveAlgorithmVkShaderPath(
  const ::agentmanager::agent::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message);

bool TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::agentmanager::agent::AlgorithmObject& object,
  const ::algorithmManager::scheduler::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

bool TryBuildAlgorithmVkExecStageSubJob(
  const ::agentmanager::agent::AlgorithmObject& object,
  const ::algorithmManager::scheduler::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

}  // namespace runtime_bridge_support
}  // namespace catalog
}  // namespace algorithmManager
