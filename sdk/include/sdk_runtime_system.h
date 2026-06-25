#pragma once

#if !defined(SDK_LAYER_INTERNAL_BUILD) && !defined(SDK_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include sdk_runtime_system.h directly. Use sdk/sdk.h."
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agent_management {
class AgentManager;
}

namespace sdk {

using AgentHandle = uint64_t;
using AlgorithmHandle = uint64_t;

struct ResourceBinding {
  std::string resource_name;
  std::string resource_kind;
  std::string source_path;
  bool required{true};
};

struct DescriptorValue {
  std::string descriptor_name;
  double scalar_value{0.0};
};

class SdkRuntimeSystem {
 public:
  SdkRuntimeSystem();
  ~SdkRuntimeSystem();
  SdkRuntimeSystem(SdkRuntimeSystem&&) noexcept;
  SdkRuntimeSystem& operator=(SdkRuntimeSystem&&) noexcept;
  SdkRuntimeSystem(const SdkRuntimeSystem&) = delete;
  SdkRuntimeSystem& operator=(const SdkRuntimeSystem&) = delete;

  AgentHandle CreateAgent(
    std::string agent_name,
    std::string* out_error_message = nullptr);
  AgentHandle CreateAgent(
    std::string agent_name,
    uint32_t limit_fps_flag,
    std::string* out_error_message = nullptr);
  bool DestroyAgent(AgentHandle agent_handle, std::string* out_error_message = nullptr);

  AlgorithmHandle MountAlgorithm(
    AgentHandle agent_handle,
    const std::string& algorithm_name,
    std::string* out_error_message = nullptr);
  bool UnmountAlgorithm(AlgorithmHandle algorithm_handle, std::string* out_error_message = nullptr);

  bool MountResource(
    AlgorithmHandle algorithm_handle,
    const std::string& resource_name,
    const std::string& resource_kind,
    const std::string& source_path,
    std::string* out_error_message = nullptr);

  bool SubmitAlgorithm(
    AlgorithmHandle algorithm_handle,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr);

  void Reset();

 private:
  struct AgentRecord {
    AgentHandle handle{0};
    size_t agent_index{0u};
  };

  struct AlgorithmDraft {
    AlgorithmHandle handle{0};
    AgentHandle agent_handle{0};
    size_t agent_index{0u};
    std::string algorithm_name;
    std::vector<ResourceBinding> resource_bindings;
    std::vector<DescriptorValue> descriptor_values;
    bool submitted{false};
    size_t submitted_algorithm_index{0u};
  };

  std::unique_ptr<agent_management::AgentManager> agent_manager_{};
  std::vector<AgentRecord> agents_{};
  std::vector<AlgorithmDraft> drafts_{};
  AgentHandle next_agent_handle_{1};
  AlgorithmHandle next_algorithm_handle_{1};

  AgentRecord* FindAgentRecord(AgentHandle agent_handle);
  const AgentRecord* FindAgentRecord(AgentHandle agent_handle) const;
  AlgorithmDraft* FindDraft(AlgorithmHandle algorithm_handle);
  const AlgorithmDraft* FindDraft(AlgorithmHandle algorithm_handle) const;
  void ShiftAgentIndicesAfterErase(size_t erased_agent_index);
  void ShiftSubmittedAlgorithmIndicesAfterErase(AgentHandle agent_handle, size_t erased_algorithm_index);
};

}  // namespace sdk
