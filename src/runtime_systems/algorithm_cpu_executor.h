#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/algorithm_cpu_executor.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include "algorithm_management/algorithm_abi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace runtime_systems {

struct CpuPipelineRegistration {
  std::string pipeline_name;
  std::string root_stage_name;
  uint32_t stage_count{0u};
  algorithm_management::AlgorithmPipelineSubmissionMode submission_mode{
    algorithm_management::AlgorithmPipelineSubmissionMode::NonCircular};
  std::string mandatory_stage_buffer_slot_name;
};

struct CpuPendingPipelineStage0Submission {
  uint64_t lane_id{0u};
  bool loop_lane_active{false};
  std::shared_ptr<algorithm::AlgorithmContainerSet> prepared_container_set{};
  std::vector<algorithm_management::AlgorithmResourceBinding> resource_bindings;
  std::vector<algorithm_management::AlgorithmDescriptorValue> descriptor_values;
};

struct CpuPipelineInterStageBufferRuntimeState {
  std::string standard_container_slot_name{};
  bool valid{false};
};

struct CpuPipelineLaneRuntimeState {
  uint64_t lane_id{0u};
  bool loop_lane_active{false};
  std::shared_ptr<algorithm::AlgorithmContainerSet> standard_container_set{};
  std::vector<algorithm_management::AlgorithmResourceBinding> resource_bindings{};
  std::vector<algorithm_management::AlgorithmDescriptorValue> descriptor_values{};
  std::vector<bool> stage_has_data{};
  CpuPipelineInterStageBufferRuntimeState inter_stage_buffer{};
  bool valid{false};
};

struct CpuPipelineRuntimeState {
  algorithm_management::AlgorithmPipelineSubmissionMode submission_mode{
    algorithm_management::AlgorithmPipelineSubmissionMode::NonCircular};
  uint64_t next_lane_id{1u};
  uint64_t current_lane_id{0u};
  std::string mandatory_stage_buffer_slot_name{};
  std::vector<CpuPipelineLaneRuntimeState> lanes{};
  std::vector<bool> stage_has_data{};
  std::vector<CpuPendingPipelineStage0Submission> pending_stage0_submissions{};
  bool circular_stage0_saturated{false};
};

class AlgorithmCpuExecutor {
 public:
  static AlgorithmCpuExecutor& Instance();

  void Clear();

  bool ExecuteCpuTick(
    const algorithm_management::AlgorithmObject& object,
    const algorithm_management::AgentTickContext& context,
    const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* container_set,
    common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
    algorithm_management::AlgorithmPackageDebugState* out_debug_state,
    std::string* out_error_message);

  bool RegisterPipeline(
    const CpuPipelineRegistration& registration,
    std::string* out_error_message);

  bool RegisterPipelineRuntime(
    const std::string& pipeline_name,
    const CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message);

  bool EnqueuePipelineStage0Submission(
    const std::string& pipeline_name,
    const std::string& stage0_algorithm_name,
    const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
    std::string* out_error_message);

  bool TickMountedPipeline(
    std::vector<algorithm_management::AlgorithmObject>* mounted_objects,
    size_t begin_index,
    size_t end_index,
    const algorithm_management::AgentTickContext& context,
    const std::vector<bool>& allow_tick_mask,
    const std::vector<algorithm_management::AlgorithmAssemblyState>& assembly_states,
    std::vector<algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
    common_data::AlgorithmToAgentSignal* out_pipeline_signal,
    bool* out_pipeline_processing_failed,
    std::string* out_error_message);

  void UnregisterPipeline(const std::string& pipeline_name);

  bool TryGetPipelineRegistration(
    const std::string& pipeline_name,
    CpuPipelineRegistration* out_registration) const;

  bool TryGetPipelineRuntime(
    const std::string& pipeline_name,
    CpuPipelineRuntimeState* out_runtime_state) const;

  bool UpdatePipelineRuntime(
    const std::string& pipeline_name,
    const CpuPipelineRuntimeState& runtime_state,
    std::string* out_error_message);

 private:
  AlgorithmCpuExecutor() = default;
};

bool TryExecuteCpuTick(
  const algorithm_management::AlgorithmObject& object,
  const algorithm_management::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  algorithm_management::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

}  // namespace runtime_systems
