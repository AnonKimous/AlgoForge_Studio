#pragma once

#if !defined(SDK_LAYER_INTERNAL_BUILD) && !defined(SDK_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include sdk_decomposer.h directly. Use sdk/sdk.h."
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sdk {

struct RequestedResource {
  std::string resource_name;
  std::string resource_kind;
  bool required{true};
};

struct RequestedDescriptorBinding {
  std::string descriptor_name;
  std::string container_name;
  uint32_t array_index{0u};
};

struct AlgorithmPackageDecomposer {
  std::string algorithm_name;
  std::vector<RequestedResource> requested_resources;
  std::vector<RequestedDescriptorBinding> requested_descriptor_bindings;
};

// External SDK surface: decomposer-only, no UI and no reflector/intervention hooks.
bool CreateAlgorithmPackageDecomposerByName(
  const std::string& algorithm_name,
  std::shared_ptr<AlgorithmPackageDecomposer>* out_decomposer,
  std::string* out_error_message = nullptr);

}  // namespace sdk
