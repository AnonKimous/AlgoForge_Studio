#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#define ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_intervention.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_types.h"
#undef ALGORITHM_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/runtime_systems.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

namespace agent_management {
struct AlgorithmPipelineStallReport;
bool ReportAlgorithmPipelineStall(
  const AlgorithmPipelineStallReport& report,
  std::string* out_error_message);
}  // namespace agent_management

namespace algorithm_management {

using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerManifest;
using ::algorithm::AlgorithmContainerManifestItem;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmPackageLocation;
using ::algorithm::AlgorithmProfile;
using ::algorithm::AlgorithmReflectionBinding;
using ::algorithm::AlgorithmReflector;
using ::algorithm::AlgorithmReflectorManifestItem;
using ::algorithm_management::AlgorithmPipelineStageSubmission;
using ::algorithm::AlgorithmStandardContainerLayout;
using ::algorithm::FindAlgorithmContainer;
using ::algorithm::TryResolveAlgorithmPackageLocation;

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

inline bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  if (!out_resource_bindings || !out_descriptor_values) {
    if (out_error_message) {
      *out_error_message = "Default binding output pointers are null.";
    }
    return false;
  }

  out_resource_bindings->clear();
  out_descriptor_values->clear();
  if (out_has_default_file) {
    *out_has_default_file = false;
  }

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

  return algorithm_support::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool FinalizeAlgorithmObject(
  const ::agent::AlgorithmObject& algorithm_object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "AlgorithmContainerSet output pointer is null.";
    }
    return false;
  }

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

  const bool ok = algorithm_support::DecomposeAlgorithmPackageFromLocation(
    package_location,
    algorithm_object.resource_bindings,
    algorithm_object.descriptor_values,
    container_set,
    out_error_message);
  if (ok && out_error_message) {
    out_error_message->clear();
  }
  return ok;
}

inline bool SubmitAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Algorithm submit output pointer is null.";
    }
    return false;
  }

  if (object.execution_preference == AlgorithmExecutionPreference::Gpu) {
    if (!object.gpu_symbol) {
      if (out_error_message) {
        *out_error_message = "Ready GPU algorithm is missing a GPU symbol.";
      }
      return false;
    }

    if (!runtime_systems::job_gpu::Execute(
          object,
          container_set,
          context,
          out_error_message)) {
      return false;
    }

    if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
      if (!runtime_systems::job_gpu::Synchronize(
            object,
            container_set,
            out_error_message)) {
        return false;
      }
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  if (!object.cpu_symbol || !object.temporaryTest_main_thread_executor) {
    if (out_error_message) {
      *out_error_message = "Ready CPU algorithm is missing its main-thread executor.";
    }
    return false;
  }

  const bool ok = runtime_systems::job_cpu::Execute(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
  if (ok && out_error_message) {
    out_error_message->clear();
  }
  return ok;
}

inline IoBufferPacket BuildAlgorithmInterventionPacket(
  const AlgorithmInterventionDescriptor& descriptor) {
  return algorithm_support::BuildAlgorithmInterventionPacket(descriptor);
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  DecodedAlgorithmIntervention* decoded) {
  return algorithm_support::DecodeAlgorithmInterventionPacket(packet, decoded);
}

inline IoBufferPacket BuildAlgorithmInterventionPacket(
  const InteractionInterventionRequest& request) {
  return algorithm_support::BuildAlgorithmInterventionPacket(request);
}

inline bool DecodeAlgorithmInterventionPacket(
  const IoBufferPacket& packet,
  InteractionInterventionRequest* request) {
  return algorithm_support::DecodeAlgorithmInterventionPacket(packet, request);
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
using algorithm_management::PipelineStageBridge;
using algorithm_management::AlgorithmRuntimeTransferBinding;
using algorithm_management::AlgorithmRuntimeTransferEdge;
using algorithm_management::AlgorithmRuntimeTransferMap;
using algorithm_management::AlgorithmPackageLocation;
using algorithm_management::FindAlgorithmContainer;
using algorithm_management::CreateAlgorithmObjectFromLocation;
using algorithm_management::FinalizeAlgorithmObject;
using algorithm_management::PipelineStageBridgeIngress;
using algorithm_management::PipelineStageBridgeEgress;
using algorithm_management::LoadAlgorithmPackageTransferMapFromLocation;
using algorithm_management::QueryAlgorithmRequestedBindings;
using algorithm_management::LoadAlgorithmPackageDefaultBindings;
using algorithm_management::SubmitAlgorithmObject;
using algorithm_management::TryResolveAlgorithmPackageLocation;
using agent_management::AlgorithmPipelineStallReport;
using agent_management::ReportAlgorithmPipelineStall;
