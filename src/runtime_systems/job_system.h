#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/job_system.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include "algorithm_management/algorithm_abi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace runtime_systems {

struct CpuPipelineRegistration {
  std::string pipeline_name;
  std::string root_stage_name;
  uint32_t stage_count{0u};
  algorithm_management::AlgorithmPipelineTopology topology{
    algorithm_management::AlgorithmPipelineTopology::NonCircular};
  algorithm_management::AlgorithmPipelineSyncMode sync_mode{
    algorithm_management::AlgorithmPipelineSyncMode::Forced};
  uint32_t max_concurrent_stage0_submissions{0u};
  std::string mandatory_stage_buffer_slot_name;
};

struct CpuPendingPipelineStage0Submission {
  std::string owner_agent_name{};
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
  std::string owner_agent_name{};
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
  std::string owner_agent_name{};
  algorithm_management::AlgorithmPipelineTopology topology{
    algorithm_management::AlgorithmPipelineTopology::NonCircular};
  algorithm_management::AlgorithmPipelineSyncMode sync_mode{
    algorithm_management::AlgorithmPipelineSyncMode::Forced};
  uint32_t max_concurrent_stage0_submissions{0u};
  uint64_t next_lane_id{1u};
  uint64_t current_lane_id{0u};
  std::string mandatory_stage_buffer_slot_name{};
  std::vector<CpuPipelineLaneRuntimeState> lanes{};
  std::vector<bool> stage_has_data{};
  std::vector<CpuPendingPipelineStage0Submission> pending_stage0_submissions{};
  algorithm_management::AlgorithmReflectionSnapshot exit_reflection_snapshot{};
  bool exit_reflection_snapshot_valid{false};
  bool stage0_saturated{false};
};

bool InitializeJobSystem(size_t worker_count = 7u);
void ShutdownJobSystem();
bool IsJobSystemInitialized();

namespace job_cpu {

void Clear();

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  const algorithm_management::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  algorithm_management::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr);

bool RegisterPipeline(
  const CpuPipelineRegistration& registration,
  std::string* out_error_message = nullptr);

bool RegisterPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message = nullptr);

bool EnqueuePipelineStage0Submission(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const std::string& stage0_algorithm_name,
  const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message = nullptr);

bool TickMountedPipeline(
  std::vector<algorithm_management::AlgorithmObject>* mounted_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& owner_agent_name,
  const algorithm_management::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<algorithm_management::AlgorithmAssemblyState>& assembly_states,
  std::vector<algorithm_management::AgentAlgorithmRuntimeState>* inout_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message = nullptr);

void UnregisterPipeline(const std::string& pipeline_name, const std::string& owner_agent_name);

bool TryGetPipelineRegistration(
  const std::string& pipeline_name,
  CpuPipelineRegistration* out_registration);

bool TryGetPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  CpuPipelineRuntimeState* out_runtime_state);

bool UpdatePipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const CpuPipelineRuntimeState& runtime_state,
  std::string* out_error_message = nullptr);

}  // namespace job_cpu

namespace job_gpu {

void Clear();

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const algorithm_management::AgentTickContext& context,
  std::string* out_error_message = nullptr);

bool Synchronize(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

}  // namespace job_gpu

}  // namespace runtime_systems
