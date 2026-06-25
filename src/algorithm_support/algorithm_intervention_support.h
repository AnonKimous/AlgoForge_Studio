#pragma once

#include "algorithm_support/algorithm_abi.h"
#include "algorithm_support/algorithm_interaction_protocol.h"

#include <memory>
#include <string>

namespace algorithm {
struct AlgorithmPackageLocation;
}  // namespace algorithm

namespace algorithm_support {

bool LoadAlgorithmInterventionFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr);

}  // namespace algorithm_support
