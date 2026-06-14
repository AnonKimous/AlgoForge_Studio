#pragma once

#include <string>

#define ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_types.h"
#include "algorithm_management/job_system.h"
#undef ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

namespace algorithm_management {

using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmStandardContainerLayout;
using ::algorithm::AlgorithmPackageLocation;
using ::algorithm::AlgorithmProfile;
using ::algorithm::AlgorithmReflectionBinding;
using ::algorithm::AlgorithmReflector;
using ::algorithm::AlgorithmReflectorManifestItem;

using ::algorithm::FindAlgorithmContainer;
using ::algorithm::TryResolveAlgorithmPackageLocation;

inline bool CreateAlgorithmPackageRuntimeReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<::algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message = nullptr) {
  return algorithm_support::CreateAlgorithmPackageRuntimeReflectorByName(
    algorithm_name,
    out_reflector,
    out_error_message);
}

inline bool CreateAlgorithmInterventionByName(
  const std::string& algorithm_name,
  std::shared_ptr<::agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message = nullptr) {
  return algorithm_support::CreateAlgorithmInterventionByName(
    algorithm_name,
    out_intervention,
    out_error_message);
}

inline bool CreateAlgorithmObjectFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  ::agent::AlgorithmObject* out_group,
  std::string* out_error_message = nullptr) {
  return algorithm_support::CreateAlgorithmObjectFromLocation(
    package_location,
    out_group,
    out_error_message);
}

inline bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  AlgorithmRequestedResources* out_requested_resources,
  AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message = nullptr) {
  if (!out_requested_resources || !out_requested_descriptor_bindings) {
    if (out_error_message) {
      *out_error_message = "Requested binding output pointers are null.";
    }
    return false;
  }

  out_requested_resources->algorithm_name = algorithm_name;
  out_requested_resources->required_resources.clear();
  out_requested_resources->valid = false;
  out_requested_descriptor_bindings->algorithm_name = algorithm_name;
  out_requested_descriptor_bindings->descriptor_slots.clear();
  out_requested_descriptor_bindings->valid = false;

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  return algorithm_support::QueryAlgorithmPackageRequestedBindingsFromLocation(
    package_location,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

inline bool DecomposeAlgorithmObject(
  const ::agent::AlgorithmObject& algorithm_object,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_object.algorithm_profile.algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_object.algorithm_profile.algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  return algorithm_support::DecomposeAlgorithmPackageFromLocation(
    package_location,
    resource_bindings,
    descriptor_values,
    container_set,
    out_error_message);
}

}  // namespace algorithm_management

using algorithm_management::AlgorithmContainer;
using algorithm_management::AlgorithmContainerSet;
using algorithm_management::AlgorithmContainerManifest;
using algorithm_management::AlgorithmContainerManifestItem;
using algorithm_management::AlgorithmContainerStorageKind;
using algorithm_management::AlgorithmStandardContainerLayout;
using algorithm_management::AlgorithmProfile;
using algorithm_management::AlgorithmReflectionBinding;
using algorithm_management::AlgorithmReflector;
using algorithm_management::AlgorithmReflectorManifestItem;
using algorithm_management::AlgorithmPackageLocation;
using algorithm_management::FindAlgorithmContainer;
using algorithm_management::CreateAlgorithmPackageRuntimeReflectorByName;
using algorithm_management::CreateAlgorithmInterventionByName;
using algorithm_management::CreateAlgorithmObjectFromLocation;
using algorithm_management::QueryAlgorithmRequestedBindings;
using algorithm_management::DecomposeAlgorithmObject;
using algorithm_management::TryResolveAlgorithmPackageLocation;
