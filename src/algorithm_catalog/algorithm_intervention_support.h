#pragma once

#include "algorithm_catalog/algorithm_abi.h"
#include "algorithm_catalog/algorithm_interaction_protocol.h"

#include <memory>
#include <string>

namespace algorithm {
struct AlgorithmPackageLocation;
}  // namespace algorithm

namespace algorithmManager { namespace catalog {

bool LoadAlgorithmInterventionFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agentmanager::agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr);

}  // namespace catalog
}  // namespace algorithmManager

