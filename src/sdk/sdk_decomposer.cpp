#define SDK_LAYER_INTERNAL_BUILD 1
#include "sdk_decomposer.h"
#undef SDK_LAYER_INTERNAL_BUILD

#include "agent/agent.h"

#include <utility>

namespace sdk {

bool CreateAlgorithmPackageDecomposerByName(
  const std::string& algorithm_name,
  std::shared_ptr<AlgorithmPackageDecomposer>* out_decomposer,
  std::string* out_error_message) {
  if (!out_decomposer) {
    if (out_error_message) {
      *out_error_message = "Decomposer output pointer is null.";
    }
    return false;
  }

  agent::AlgorithmRequestedResources requested_resources{};
  agent::AlgorithmRequestedDescriptorBindings requested_descriptor_bindings{};
  if (!agent::QueryAlgorithmRequestedBindingsByName(
        algorithm_name,
        &requested_resources,
        &requested_descriptor_bindings,
        out_error_message)) {
    return false;
  }

  std::vector<RequestedResource> sdk_requested_resources{};
  sdk_requested_resources.reserve(requested_resources.required_resources.size());
  for (const agent::AlgorithmRequestedResources::RequiredResource& resource : requested_resources.required_resources) {
    sdk_requested_resources.push_back(RequestedResource{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .required = resource.required,
    });
  }

  std::vector<RequestedDescriptorBinding> sdk_requested_descriptor_bindings{};
  sdk_requested_descriptor_bindings.reserve(requested_descriptor_bindings.descriptor_slots.size());
  for (const agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot& slot :
       requested_descriptor_bindings.descriptor_slots) {
    sdk_requested_descriptor_bindings.push_back(RequestedDescriptorBinding{
      .descriptor_name = slot.descriptor_name,
      .container_name = slot.container_name,
      .array_index = slot.array_index,
    });
  }

  auto decomposer = std::make_shared<AlgorithmPackageDecomposer>();
  decomposer->algorithm_name = requested_resources.algorithm_name.empty() ? algorithm_name : requested_resources.algorithm_name;
  decomposer->requested_resources = std::move(sdk_requested_resources);
  decomposer->requested_descriptor_bindings = std::move(sdk_requested_descriptor_bindings);
  *out_decomposer = std::move(decomposer);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace sdk
