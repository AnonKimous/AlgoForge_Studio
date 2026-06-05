#pragma once

#define ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "algorithm_management/algorithm_container_manifest.h"
#include "algorithm_management/algorithm_types.h"
#undef ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

namespace algorithm_management {

using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmProfile;
using ::algorithm::AlgorithmReflectionBinding;
using ::algorithm::AlgorithmReflector;
using ::algorithm::AlgorithmReflectorManifestItem;

using ::algorithm::CreateAlgorithmContainersFromManifestFile;
using ::algorithm::CreateAlgorithmContainersFromManifestName;
using ::algorithm::CreateAlgorithmReflectorFromManifestFile;
using ::algorithm::CreateAlgorithmReflectorFromManifestName;
using ::algorithm::FindAlgorithmContainer;
using ::algorithm::ResolveAlgorithmManifestName;
using ::algorithm::TryCreateAlgorithmReflectorFromAlgorithmName;

// Official management-layer entrypoints for the algorithm-management module.
// Runtime containers are created only from official container manifests.
inline bool AlgorithmManager_CreateContainersFromManifestFile(
  const std::string& path,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message = nullptr) {
  return ::algorithm::CreateAlgorithmContainersFromManifestFile(path, out_container_set, out_error_message);
}

inline bool AlgorithmManager_CreateContainersFromManifestName(
  const std::string& manifest_name,
  AlgorithmContainerSet* out_container_set,
  std::string* out_error_message = nullptr) {
  return ::algorithm::CreateAlgorithmContainersFromManifestName(manifest_name, out_container_set, out_error_message);
}

inline bool AlgorithmManager_CreateReflectorFromManifestFile(
  const std::string& path,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr) {
  return ::algorithm::CreateAlgorithmReflectorFromManifestFile(path, out_reflector, out_error_message);
}

inline bool AlgorithmManager_CreateReflectorFromManifestName(
  const std::string& manifest_name,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr) {
  return ::algorithm::CreateAlgorithmReflectorFromManifestName(manifest_name, out_reflector, out_error_message);
}

inline bool AlgorithmManager_TryCreateReflectorFromAlgorithmName(
  const std::string& algorithm_name,
  AlgorithmReflector* out_reflector,
  std::string* out_error_message = nullptr) {
  return ::algorithm::TryCreateAlgorithmReflectorFromAlgorithmName(algorithm_name, out_reflector, out_error_message);
}

}  // namespace algorithm_management

using algorithm_management::AlgorithmContainer;
using algorithm_management::AlgorithmManager_CreateContainersFromManifestFile;
using algorithm_management::AlgorithmManager_CreateContainersFromManifestName;
using algorithm_management::AlgorithmManager_CreateReflectorFromManifestFile;
using algorithm_management::AlgorithmManager_CreateReflectorFromManifestName;
using algorithm_management::AlgorithmManager_TryCreateReflectorFromAlgorithmName;
using algorithm_management::AlgorithmContainerSet;
using algorithm_management::AlgorithmContainerManifest;
using algorithm_management::AlgorithmContainerManifestItem;
using algorithm_management::AlgorithmContainerStorageKind;
using algorithm_management::AlgorithmProfile;
using algorithm_management::AlgorithmReflectionBinding;
using algorithm_management::AlgorithmReflector;
using algorithm_management::AlgorithmReflectorManifestItem;
using algorithm_management::CreateAlgorithmContainersFromManifestFile;
using algorithm_management::CreateAlgorithmContainersFromManifestName;
using algorithm_management::CreateAlgorithmReflectorFromManifestFile;
using algorithm_management::CreateAlgorithmReflectorFromManifestName;
using algorithm_management::FindAlgorithmContainer;
using algorithm_management::ResolveAlgorithmManifestName;
using algorithm_management::TryCreateAlgorithmReflectorFromAlgorithmName;
