#pragma once

#include "algomanager/bridge/algorithm_abi.h"
#include "algomanager/bridge/algorithm_interaction_protocol.h"

#include <memory>
#include <string>

namespace algorithm {
struct AlgorithmPackageLocation;
}  // namespace algorithm

namespace algomanager { namespace algocatalog {

bool LoadAlgorithmInterventionFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algomanager::bridge::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr);

}  // namespace catalog
}  // namespace algomanager

