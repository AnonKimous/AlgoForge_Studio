#pragma once

#include <string>

#define ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "algorithm_management/algorithm_container_manifest.h"
#include "algorithm_management/algorithm_types.h"
#include "algorithm_management/job_system.h"
#undef ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

namespace agent {
struct AgentAlgorithmCodecGroup;
struct AgentTickContext;
}

namespace algorithm_management {

using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmStandardContainerLayout;
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
using algorithm_management::CreateAlgorithmContainersFromManifestFile;
using algorithm_management::CreateAlgorithmContainersFromManifestName;
using algorithm_management::CreateAlgorithmReflectorFromManifestFile;
using algorithm_management::CreateAlgorithmReflectorFromManifestName;
using algorithm_management::FindAlgorithmContainer;
using algorithm_management::ResolveAlgorithmManifestName;
using algorithm_management::TryCreateAlgorithmReflectorFromAlgorithmName;
