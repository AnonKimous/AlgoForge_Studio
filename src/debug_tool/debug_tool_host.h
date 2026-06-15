#pragma once

#include "common_data/common_data.h"
#include "runtime_systems/runtime_environment.h"

#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

namespace debug_tool {

struct AlgorithmCatalogEntry {
  std::string algorithm_name;
  std::string display_name;
  std::string folder_name;
  std::string container_manifest_name;
  std::string decomposer_name;
  std::string reflector_name;
  std::string intervention_name;
};

struct RequestedResourceEntry {
  std::string resource_name;
  std::string resource_kind;
  bool required{true};
};

struct RequestedDescriptorEntry {
  std::string descriptor_name;
  std::string container_name;
  uint32_t array_index{0u};
};

struct AlgorithmResourceBinding {
  std::string resource_name;
  std::string resource_kind;
  std::string source_path;
  bool required{true};
};

struct AlgorithmDescriptorValue {
  std::string descriptor_name;
  float scalar_value{0.0f};
};

enum class AlgorithmMountMode {
  Direct = 0,
  StandardContainer = 1,
};

enum class AlgorithmExecutionPreference {
  Cpu = 0,
  Gpu = 1,
};

enum class AlgorithmAssemblyState {
  Pending = 0,
  Assembling = 1,
  Ready = 2,
  Failed = 3,
};

struct AlgorithmReflectionValue {
  std::string reflection_object_name;
  std::string container_name;
  std::string filter_name;
  std::string storage_kind;
  std::vector<std::byte> bytes;
};

struct AlgorithmReflectionSnapshot {
  std::string algorithm_name;
  std::vector<AlgorithmReflectionValue> variables;
  std::vector<AlgorithmReflectionValue> variable_arrays;
  bool valid{false};

  void Clear() {
    algorithm_name.clear();
    variables.clear();
    variable_arrays.clear();
    valid = false;
  }
};

struct AlgorithmRuntimeSummary {
  std::string algorithm_name;
  AlgorithmAssemblyState assembly_state{AlgorithmAssemblyState::Failed};
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
  bool cpu_symbol{true};
  bool gpu_symbol{true};
  AlgorithmMountMode mount_mode{AlgorithmMountMode::Direct};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Gpu};
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  AlgorithmReflectionSnapshot reflection_snapshot{};
};

struct AgentRuntimeSummary {
  std::string agent_name;
  std::vector<AlgorithmRuntimeSummary> algorithms;
};

class IDebugToolHost {
 public:
  virtual ~IDebugToolHost() = default;

  virtual bool has_agents() const = 0;
  virtual size_t agent_count() const = 0;
  virtual bool GetAgentSummary(size_t agent_index, AgentRuntimeSummary* out_summary) const = 0;
  virtual const AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const = 0;

  virtual bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<AlgorithmResourceBinding>& resource_bindings,
    const std::vector<AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    AlgorithmMountMode mount_mode = AlgorithmMountMode::Direct,
    AlgorithmExecutionPreference execution_preference = AlgorithmExecutionPreference::Gpu) = 0;
  virtual bool DetachAlgorithmFromAgent(
    size_t agent_index,
    size_t algorithm_index,
    std::string* out_error_message = nullptr) = 0;
  virtual bool HotReloadAlgorithmPackage(
    size_t agent_index,
    size_t algorithm_index,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr) = 0;
  virtual void StartTicking() = 0;
  virtual void PauseTicking() = 0;
  virtual bool tick_enabled() const = 0;
  virtual bool TickManagedAgents() = 0;
  virtual void ClearAgents() = 0;
  virtual void ClearGpuExecutors() = 0;
  virtual bool LoadAlgorithmCatalog(
    std::vector<AlgorithmCatalogEntry>* out_entries,
    std::string* out_error_message = nullptr) const = 0;
  virtual bool QueryAlgorithmRequestedBindings(
    const std::string& algorithm_name,
    std::vector<RequestedResourceEntry>* out_resources,
    std::vector<RequestedDescriptorEntry>* out_descriptors,
    std::string* out_error_message = nullptr) const = 0;
  virtual bool LoadAlgorithmPackageDefaultBindings(
    const std::string& algorithm_name,
    std::vector<AlgorithmResourceBinding>* out_resource_bindings,
    std::vector<AlgorithmDescriptorValue>* out_descriptor_values,
    bool* out_has_default_file = nullptr,
    std::string* out_error_message = nullptr) const = 0;
  virtual bool BuildRenderPreviewRequest(
    size_t agent_index,
    size_t algorithm_index,
    runtime_systems::RenderPreviewRequest* out_request,
    std::string* out_error_message = nullptr) const = 0;

  virtual const InputState& input() const = 0;
  virtual Vec2 mouse_position() const = 0;
  virtual float frame_dt_seconds() const = 0;
  virtual bool has_render_preview_texture() const = 0;
  virtual std::string render_preview_debug_summary() const = 0;
  virtual ImTextureID render_preview_texture_id() const = 0;
  virtual ImVec2 render_preview_texture_size() const = 0;
  virtual void SetRenderPreviewExtent(ImVec2 extent) = 0;
  virtual void SetRenderPreviewRequest(runtime_systems::RenderPreviewRequest request) = 0;

  virtual std::string& ui_status_message() = 0;
  virtual const std::string& ui_status_message() const = 0;
};

}  // namespace debug_tool

