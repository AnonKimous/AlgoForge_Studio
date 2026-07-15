#pragma once

#include "algorithm_catalog/algorithm_data.h"
#include "algorithm_catalog/algorithm_types.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithmManager { namespace scheduler {

struct AlgorithmRequestedResources {
  struct RequiredResource {
    std::string resource_name;
    std::string resource_kind;
    bool required{true};
  };

  std::string algorithm_name;
  std::vector<RequiredResource> required_resources;
  bool valid{false};
};

struct AlgorithmRequestedDescriptorBindings {
  struct DescriptorSlot {
    std::string descriptor_name;
    std::string container_name;
    uint32_t array_index{0u};
  };

  std::string algorithm_name;
  std::vector<DescriptorSlot> descriptor_slots;
  bool valid{false};
};

struct AlgorithmResourceBinding {
  std::string resource_name;
  std::string resource_kind;
  std::string source_path;
};

struct AlgorithmDescriptorValue {
  std::string descriptor_name;
  double scalar_value{0.0};
};

struct AlgorithmPipelineStageRuntimeStat {
  std::string stage_name;
  float elapsed_seconds{0.0f};
  std::string reason;
};

struct PipelineLaneTimingStat {
  uint64_t lane_id{0u};
  float elapsed_seconds{0.0f};
  bool valid{false};
};

struct PipelineTimingSnapshot {
  std::string pipeline_name;
  float total_elapsed_seconds{0.0f};
  std::vector<AlgorithmPipelineStageRuntimeStat> stage_timings;
  std::vector<PipelineLaneTimingStat> lane_timings;
  bool valid{false};
};

enum class AlgorithmMountMode {
  Direct = 0,
  StandardContainer = 1,
  Pipeline = 2,
};

enum class AlgorithmExecutionPreference {
  Jobs = 0,
  Vk = 1,
  Cuda = 2,
  Compatibility = 3,
};

struct AlgorithmPipelineStageSubmission {
  std::string stage_name;
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Vk};
};

enum class AlgorithmPipelineTopology {
  NonCircular = 0,
  Circular = 1,
};

enum class AlgorithmPipelineSyncMode {
  Forced = 0,
  NonForced = 1,
};

enum class AlgorithmPipelineWrapperRole {
  None = 0,
  Begin = 1,
  End = 2,
};

using AlgorithmPipelineSubmissionMode = AlgorithmPipelineTopology;

enum class AlgorithmJobPriority {
  High = 0,
  Normal = 1,
  Low = 2,
};

enum class AlgorithmTickLifetime {
  Continuous = 0,
  LaunchOnceThenHold = 1,
};

enum class AlgorithmExecutionPhase {
  Pretick = 0,
  Exec = 1,
  AfterTick = 2,
  RenderResult = 3,
  Reflect = 4,
  Body = Exec,
  PreExecution = Pretick,
  PostExecution = AfterTick,
  ResultRender = RenderResult,
};

struct AlgorithmReflectionValue {
  std::string reflection_object_name;
  std::string container_name;
  std::string filter_name;
  algorithm::AlgorithmContainerStorageKind storage_kind{algorithm::AlgorithmContainerStorageKind::Array};
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

struct JobsPipelineRegistration {
  std::string pipeline_name;
  std::string root_stage_name;
  uint32_t stage_count{0u};
  uint32_t body_begin_stage_index{0u};
  uint32_t body_stage_count{0u};
  uint32_t effective_tail_stage_index{0u};
  AlgorithmPipelineTopology topology{AlgorithmPipelineTopology::NonCircular};
  AlgorithmPipelineSyncMode sync_mode{AlgorithmPipelineSyncMode::Forced};
  uint32_t max_concurrent_stage0_submissions{0u};
  std::string mandatory_stage_buffer_slot_name;
};

struct JobsPendingPipelineStage0Submission {
  std::string owner_agent_name{};
  uint64_t lane_id{0u};
  bool loop_lane_active{false};
  std::shared_ptr<algorithm::AlgorithmContainerSet> prepared_container_set{};
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
};

struct JobsPipelineInterStageBufferRuntimeState {
  std::string standard_container_slot_name{};
  uint32_t scalar_slot_count{0u};
  std::vector<float> scalar_slots{};
  bool valid{false};
};

struct JobsPipelineLaneRuntimeState {
  std::string owner_agent_name{};
  uint64_t lane_id{0u};
  bool loop_lane_active{false};
  std::shared_ptr<algorithm::AlgorithmContainerSet> standard_container_set{};
  std::vector<AlgorithmResourceBinding> resource_bindings{};
  std::vector<AlgorithmDescriptorValue> descriptor_values{};
  std::vector<bool> stage_has_data{};
  JobsPipelineInterStageBufferRuntimeState inter_stage_buffer{};
  bool valid{false};
};

struct JobsPipelineRuntimeState {
  std::string owner_agent_name{};
  AlgorithmPipelineTopology topology{AlgorithmPipelineTopology::NonCircular};
  AlgorithmPipelineSyncMode sync_mode{AlgorithmPipelineSyncMode::Forced};
  uint32_t max_concurrent_stage0_submissions{0u};
  uint64_t next_lane_id{1u};
  uint64_t current_lane_id{0u};
  std::string mandatory_stage_buffer_slot_name{};
  std::vector<JobsPipelineLaneRuntimeState> lanes{};
  std::vector<bool> stage_has_data{};
  std::vector<JobsPendingPipelineStage0Submission> pending_stage0_submissions{};
  AlgorithmReflectionSnapshot exit_reflection_snapshot{};
  bool exit_reflection_snapshot_valid{false};
  bool stage0_saturated{false};
};

struct AlgorithmPackageDebugState {
  std::vector<AdvancedAlgorithmDebugSignal> signals;
};

struct PipelineStageBridgeDebugBinding {
  std::string source_stage_name;
  std::string target_stage_name;
  std::string source_container_name;
  std::string target_container_name;
  bool required{true};
};

struct PipelineStageBridgeDebugSet {
  std::string pipeline_name;
  std::string stage_name;
  std::string previous_stage_name;
  std::string next_stage_name;
  std::vector<PipelineStageBridgeDebugBinding> ingress_bindings;
  std::vector<PipelineStageBridgeDebugBinding> egress_bindings;
  algorithm::AlgorithmContainerSet stage_input_container_set{};
  algorithm::AlgorithmContainerSet stage_output_container_set{};
  algorithm::AlgorithmContainerSet next_stage_input_container_set{};
  algorithm::AlgorithmContainerSet replay_output_container_set{};
  AlgorithmPackageDebugState replay_debug_state{};
  AlgorithmReflectionSnapshot replay_reflection_snapshot{};
  AlgorithmToAgentSignal replay_algorithm_to_agent_signal{};
  bool has_stage_input_container_set{false};
  bool has_stage_output_container_set{false};
  bool has_next_stage_input_container_set{false};
  bool has_replay_output_container_set{false};
  bool replay_valid{false};
  bool valid{false};

  void Clear() {
    pipeline_name.clear();
    stage_name.clear();
    previous_stage_name.clear();
    next_stage_name.clear();
    ingress_bindings.clear();
    egress_bindings.clear();
    stage_input_container_set = {};
    stage_output_container_set = {};
    next_stage_input_container_set = {};
    replay_output_container_set = {};
    replay_debug_state = {};
    replay_reflection_snapshot.Clear();
    replay_algorithm_to_agent_signal = {};
    has_stage_input_container_set = false;
    has_stage_output_container_set = false;
    has_next_stage_input_container_set = false;
    has_replay_output_container_set = false;
    replay_valid = false;
    valid = false;
  }
};

struct AlgorithmInterventionPackageDebugState {
  std::vector<AdvancedAlgorithmDebugSignal> signals;
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
};

struct AlgorithmVkExecContainerBinding {
  std::string container_name;
  std::string container_kind;
  uint32_t tuple_width{0u};
  bool required{true};
};

struct AlgorithmVkExecShaderSpec {
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::string pipeline_kind;
};

struct AlgorithmVkExecSpec {
  std::string stage_name{"exec"};
  std::vector<std::string> functions;
  std::vector<AlgorithmVkExecContainerBinding> used_algorithm_containers;
  AlgorithmVkExecShaderSpec shader;
};

struct AgentTickContext {
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  Vec2 render_preview_extent{1024.0f, 1024.0f};
  float dt_seconds{0.0f};
  AlgorithmExecutionPhase execution_phase{AlgorithmExecutionPhase::Exec};
  AlgorithmJobPriority job_priority{AlgorithmJobPriority::High};
  const InteractionInterventionRequest* intervention_request{nullptr};
};

struct AgentAlgorithmRuntimeState {
  std::string algorithm_name;
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  AlgorithmPackageDebugState debug_state{};
  AlgorithmReflectionSnapshot reflection_snapshot{};
  float algorithm_exec_elapsed_seconds{0.0f};
  bool algorithm_exec_elapsed_valid{false};
  bool launch_once_completed{false};
  bool reflection_snapshot_cached{false};
  uint64_t pipeline_progress_signature{0u};
  bool pipeline_progress_signature_valid{false};
  float pipeline_no_progress_seconds{0.0f};
  bool pipeline_stall_report_requested{false};
  bool pipeline_stall_reported{false};
  std::string pipeline_stall_reason;
  uint32_t pipeline_active_stage_index{0u};
  bool pipeline_active_stage_index_valid{false};
  uint32_t pipeline_active_bundle_begin_stage_index{0u};
  uint32_t pipeline_active_bundle_stage_count{0u};
  AlgorithmExecutionPreference pipeline_active_bundle_preference{AlgorithmExecutionPreference::Vk};
  bool pipeline_active_bundle_valid{false};
  float pipeline_total_elapsed_seconds{0.0f};
  std::vector<AlgorithmPipelineStageRuntimeStat> pipeline_stage_runtime_stats;
  // Runtime state for child algorithm objects when this object is a composite node.
  std::vector<AgentAlgorithmRuntimeState> child_runtime_states;
  PipelineStageBridgeDebugSet bridge_debug_set{};
};

enum class AlgorithmAssemblyState {
  Pending,
  Assembling,
  Ready,
  Failed,
};

struct AgentTickResult {
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  std::vector<AgentAlgorithmRuntimeState> algorithm_runtime_states;
  std::string timing_log;
};

class IAlgorithmPackageSupport;
class IAlgorithmJobsExecutor;
class IAlgorithmVkExecutor;
class IAlgorithmCudaExecutor;
class IAlgorithmCompatibilityExecutor;
class IAlgorithmIntervention;
class AlgorithmObject;
struct AlgorithmObjectChildStorage;

struct AlgorithmAssemblySlot {
  size_t index{0u};
  AlgorithmObject* algorithm_object{nullptr};
  AlgorithmAssemblyState* assembly_state{nullptr};
};

class AlgorithmObject {
 public:
  AlgorithmObject() : shared_container_set(std::make_shared<algorithm::AlgorithmContainerSet>()) {}

  algorithm::AlgorithmContainerSet* mutable_container_set() {
    EnsureContainerSet();
    return shared_container_set.get();
  }
  const algorithm::AlgorithmContainerSet* container_set() const {
    return shared_container_set.get();
  }
  void SetContainerSet(std::shared_ptr<algorithm::AlgorithmContainerSet> container_set) {
    shared_container_set = std::move(container_set);
    EnsureContainerSet();
  }

  algorithm::AlgorithmProfile algorithm_profile{};
  std::string runtime_package_root_path;
  std::shared_ptr<IAlgorithmPackageSupport> reflector;
  std::shared_ptr<algorithm::AlgorithmReflector> algorithm_reflector;
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> runtime_transfer_map;
  std::shared_ptr<algorithm::AlgorithmContainerSet> shared_container_set;
  bool pipeline_stage{false};
  std::string pipeline_name;
  uint32_t pipeline_stage_index{0u};
  uint32_t pipeline_stage_count{0u};
  AlgorithmPipelineWrapperRole pipeline_wrapper_role{AlgorithmPipelineWrapperRole::None};
  bool pipeline_wrapper_empty{false};
  bool pipeline_stage_debug_all{true};
  uint32_t pipeline_stage_debug_index{0u};
  AlgorithmPipelineTopology pipeline_topology{AlgorithmPipelineTopology::NonCircular};
  AlgorithmPipelineSyncMode pipeline_sync_mode{AlgorithmPipelineSyncMode::Forced};
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
  std::vector<std::string> pipeline_external_write_reset_container_names;
  bool jobs_symbol{true};
  bool vk_symbol{true};
  bool cuda_symbol{true};
  bool compatibility_symbol{false};
  AlgorithmMountMode mount_mode{AlgorithmMountMode::Direct};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Vk};
  AlgorithmTickLifetime tick_lifetime{AlgorithmTickLifetime::Continuous};
  std::shared_ptr<IAlgorithmVkExecutor> vk_executor;
  std::shared_ptr<IAlgorithmCudaExecutor> cuda_executor;
  std::shared_ptr<IAlgorithmCompatibilityExecutor> compatibility_executor;
  std::shared_ptr<IAlgorithmJobsExecutor> jobs_executor;
  std::shared_ptr<IAlgorithmIntervention> intervention;
  // Every mounted algorithm is an AlgorithmObject. An object may contain
  // executable AlgorithmObjects when its scheduler requires nesting.
  std::vector<std::shared_ptr<AlgorithmObject>> child_algorithm_objects;
  std::shared_ptr<AlgorithmObjectChildStorage> child_algorithm_object_storage;

 private:
  void EnsureContainerSet() {
    if (!shared_container_set) {
      shared_container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
    }
  }
};

struct AlgorithmObjectChildStorage {
  std::vector<AlgorithmObject> children;
};

struct AgentInitConfig {
  std::string agent_name;
  std::vector<AlgorithmObject> algorithm_objects;
};

class IAlgorithmPackageSupport {
 public:
  virtual ~IAlgorithmPackageSupport() = default;

  virtual bool BuildAlgorithmProfile(
    const VolumeDescriptor& volume,
    algorithm::AlgorithmProfile* out_profile) const = 0;

  virtual bool BuildMeshCoderOutput(const Mesh& mesh, MeshCoderOutput* out_output) const {
    (void)mesh;
    (void)out_output;
    return false;
  }

  virtual bool ReflectMeshCommon(const Mesh& mesh, MeshCommonReflection* out_reflection) const {
    (void)mesh;
    (void)out_reflection;
    return false;
  }

  virtual bool BuildVolumeDescriptor(
    const Mesh& mesh,
    float mass,
    Vec3 driving_dir,
    VolumeDescriptor* out_volume) const {
    (void)mesh;
    (void)mass;
    (void)driving_dir;
    (void)out_volume;
    return false;
  }
};

class ISimpleAlgorithmPackageSupport : public IAlgorithmPackageSupport {
 public:
  ~ISimpleAlgorithmPackageSupport() override = default;
};

class IComplexAlgorithmPackageSupport : public IAlgorithmPackageSupport {
 public:
  ~IComplexAlgorithmPackageSupport() override = default;
  virtual void CollectDebugState(AlgorithmPackageDebugState* debug_state) const = 0;
};

class IAlgorithmJobsExecutor {
 public:
  virtual ~IAlgorithmJobsExecutor() = default;

  virtual bool ExecuteJobsAlgorithm(
    const AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) = 0;
};

class IAlgorithmVkExecutor {
 public:
  virtual ~IAlgorithmVkExecutor() = default;

  virtual bool GetVkExecSpec(AlgorithmVkExecSpec* out_spec) const = 0;
};

class IAlgorithmCudaExecutor {
 public:
  virtual ~IAlgorithmCudaExecutor() = default;

  virtual bool ExecuteCudaAlgorithm(
    const AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) = 0;
};

class IAlgorithmCompatibilityExecutor {
 public:
  virtual ~IAlgorithmCompatibilityExecutor() = default;

  virtual bool ExecuteCompatibleAlgorithm(
    const AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) = 0;
};

enum class AlgorithmPhaseKind {
  Pretick = 0,
  Exec = 1,
  AfterTick = 2,
  RenderResult = 3,
  Reflect = 4,
  Custom = 5,
  PreExecution = Pretick,
  InExecution = Exec,
  PostExecution = AfterTick,
  ResultRender = RenderResult,
};

using AlgorithmInterventionStageKind = AlgorithmPhaseKind;

struct AlgorithmPhaseContainerBinding {
  std::string container_name;
  std::string container_kind;
  uint32_t tuple_width{0u};
  bool required{true};
};

using AlgorithmInterventionContainerBinding = AlgorithmPhaseContainerBinding;

struct AlgorithmPhaseShaderSpec {
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::string pipeline_kind;
};

using AlgorithmInterventionShaderSpec = AlgorithmPhaseShaderSpec;

struct AlgorithmPhaseSpec {
  std::string stage_name;
  AlgorithmPhaseKind stage_kind{AlgorithmPhaseKind::Custom};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Jobs};
  std::vector<std::string> functions;
  std::vector<AlgorithmPhaseContainerBinding> used_algorithm_containers;
  AlgorithmPhaseShaderSpec shader;
};

using AlgorithmInterventionStageSpec = AlgorithmPhaseSpec;

class IAlgorithmIntervention {
 public:
  virtual ~IAlgorithmIntervention() = default;

  virtual bool SupportsIntervention() const = 0;
  virtual void FillAgentToAlgorithmSignal(
    const AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const = 0;
  virtual bool GetInterventionPhaseSpecs(
    std::vector<AlgorithmPhaseSpec>* out_phase_specs) const {
    return false;
  }
  virtual bool GetInterventionStageSpecs(
    std::vector<AlgorithmPhaseSpec>* out_stage_specs) const {
    return GetInterventionPhaseSpecs(out_stage_specs);
  }
};

}  // namespace scheduler
}  // namespace algorithmManager

namespace algorithmManager {
using ::algorithm::AlgorithmContainer;
using ::algorithm::AlgorithmContainerSet;
using ::algorithm::AlgorithmContainerStorageKind;
using ::algorithm::AlgorithmStandardContainerLayout;
using ::algorithm::AlgorithmProfile;
using ::algorithm::AlgorithmReflectionBinding;
using ::algorithm::AlgorithmReflector;
using ::algorithm::AlgorithmRuntimeTransferBinding;
using ::algorithm::AlgorithmRuntimeTransferEdge;
using ::algorithm::AlgorithmRuntimeTransferMap;
using ::algorithm::FindAlgorithmContainer;
using ::algorithmManager::scheduler::JobsPendingPipelineStage0Submission;
using ::algorithmManager::scheduler::JobsPipelineLaneRuntimeState;
using ::algorithmManager::scheduler::AgentAlgorithmRuntimeState;
using ::algorithmManager::scheduler::AgentInitConfig;
using ::algorithmManager::scheduler::AgentTickContext;
using ::algorithmManager::scheduler::AgentTickResult;
using ::algorithmManager::scheduler::AlgorithmAssemblySlot;
using ::algorithmManager::scheduler::AlgorithmAssemblyState;
using ::algorithmManager::scheduler::AlgorithmDescriptorValue;
using ::algorithmManager::scheduler::AlgorithmExecutionPhase;
using ::algorithmManager::scheduler::AlgorithmExecutionPreference;
using ::algorithmManager::scheduler::AlgorithmPipelineSubmissionMode;
using ::algorithmManager::scheduler::AlgorithmPipelineTopology;
using ::algorithmManager::scheduler::AlgorithmPipelineSyncMode;
using ::algorithmManager::scheduler::AlgorithmPipelineWrapperRole;
using ::algorithmManager::scheduler::JobsPipelineInterStageBufferRuntimeState;
using ::algorithmManager::scheduler::JobsPipelineRegistration;
using ::algorithmManager::scheduler::JobsPipelineRuntimeState;
using ::algorithmManager::scheduler::AlgorithmJobPriority;
using ::algorithmManager::scheduler::AlgorithmPhaseKind;
using ::algorithmManager::scheduler::AlgorithmInterventionContainerBinding;
using ::algorithmManager::scheduler::AlgorithmInterventionPackageDebugState;
using ::algorithmManager::scheduler::AlgorithmInterventionShaderSpec;
using ::algorithmManager::scheduler::AlgorithmInterventionStageKind;
using ::algorithmManager::scheduler::AlgorithmInterventionStageSpec;
using ::algorithmManager::scheduler::AlgorithmPhaseContainerBinding;
using ::algorithmManager::scheduler::AlgorithmPhaseShaderSpec;
using ::algorithmManager::scheduler::AlgorithmPhaseSpec;
using ::algorithmManager::scheduler::AlgorithmVkExecContainerBinding;
using ::algorithmManager::scheduler::AlgorithmVkExecShaderSpec;
using ::algorithmManager::scheduler::AlgorithmVkExecSpec;
using ::algorithmManager::scheduler::AlgorithmMountMode;
using ::algorithmManager::scheduler::AlgorithmPipelineStageSubmission;
using ::algorithmManager::scheduler::AlgorithmPipelineStageRuntimeStat;
using ::algorithmManager::scheduler::AlgorithmObject;
using ::algorithmManager::scheduler::AlgorithmPackageDebugState;
using ::algorithmManager::scheduler::AlgorithmReflectionSnapshot;
using ::algorithmManager::scheduler::AlgorithmReflectionValue;
using ::algorithmManager::scheduler::AlgorithmTickLifetime;
using ::algorithmManager::scheduler::AlgorithmRequestedDescriptorBindings;
using ::algorithmManager::scheduler::AlgorithmRequestedResources;
using ::algorithmManager::scheduler::AlgorithmResourceBinding;
using ::algorithmManager::scheduler::IAlgorithmIntervention;
using ::algorithmManager::scheduler::IAlgorithmPackageSupport;
using ::algorithmManager::scheduler::IAlgorithmJobsExecutor;
using ::algorithmManager::scheduler::IAlgorithmVkExecutor;
using ::algorithmManager::scheduler::IAlgorithmCudaExecutor;
using ::algorithmManager::scheduler::IAlgorithmCompatibilityExecutor;
using ::algorithmManager::scheduler::IComplexAlgorithmPackageSupport;
using ::algorithmManager::scheduler::ISimpleAlgorithmPackageSupport;
namespace catalog {}
namespace scheduler {}
}  // namespace algorithmManager

namespace agentmanager {
namespace agent {
using algorithmManager::scheduler::AgentAlgorithmRuntimeState;
using algorithmManager::scheduler::AgentInitConfig;
using algorithmManager::scheduler::AgentTickContext;
using algorithmManager::scheduler::AgentTickResult;
using algorithmManager::scheduler::AlgorithmAssemblySlot;
using algorithmManager::scheduler::AlgorithmAssemblyState;
using algorithmManager::AlgorithmDescriptorValue;
using algorithmManager::scheduler::AlgorithmExecutionPhase;
using algorithmManager::AlgorithmExecutionPreference;
using algorithmManager::scheduler::AlgorithmPipelineSubmissionMode;
using algorithmManager::AlgorithmPipelineTopology;
using algorithmManager::AlgorithmPipelineSyncMode;
using algorithmManager::scheduler::JobsPipelineInterStageBufferRuntimeState;
using algorithmManager::scheduler::JobsPipelineRegistration;
using algorithmManager::scheduler::JobsPipelineRuntimeState;
using algorithmManager::scheduler::AlgorithmJobPriority;
using algorithmManager::AlgorithmPhaseKind;
using algorithmManager::AlgorithmInterventionContainerBinding;
using algorithmManager::scheduler::AlgorithmInterventionPackageDebugState;
using algorithmManager::scheduler::AlgorithmInterventionShaderSpec;
using algorithmManager::AlgorithmInterventionStageKind;
using algorithmManager::AlgorithmInterventionStageSpec;
using algorithmManager::AlgorithmPhaseContainerBinding;
using algorithmManager::AlgorithmPhaseShaderSpec;
using algorithmManager::AlgorithmPhaseSpec;
using algorithmManager::scheduler::AlgorithmVkExecContainerBinding;
using algorithmManager::scheduler::AlgorithmVkExecShaderSpec;
using algorithmManager::scheduler::AlgorithmVkExecSpec;
using algorithmManager::AlgorithmMountMode;
using algorithmManager::AlgorithmPipelineStageSubmission;
using algorithmManager::AlgorithmPipelineStageRuntimeStat;
using algorithmManager::scheduler::AlgorithmObject;
using algorithmManager::scheduler::AlgorithmPackageDebugState;
using algorithmManager::scheduler::PipelineStageBridgeDebugBinding;
using algorithmManager::scheduler::PipelineStageBridgeDebugSet;
using algorithmManager::scheduler::AlgorithmReflectionSnapshot;
using algorithmManager::scheduler::AlgorithmReflectionValue;
using algorithmManager::AlgorithmTickLifetime;
using algorithmManager::AlgorithmRequestedDescriptorBindings;
using algorithmManager::AlgorithmRequestedResources;
using algorithmManager::AlgorithmResourceBinding;
using algorithmManager::scheduler::IAlgorithmIntervention;
using algorithmManager::scheduler::IAlgorithmPackageSupport;
using algorithmManager::scheduler::IAlgorithmJobsExecutor;
using algorithmManager::scheduler::IAlgorithmVkExecutor;
using algorithmManager::scheduler::IAlgorithmCudaExecutor;
using algorithmManager::scheduler::IAlgorithmCompatibilityExecutor;
using algorithmManager::scheduler::IComplexAlgorithmPackageSupport;
using algorithmManager::scheduler::ISimpleAlgorithmPackageSupport;
}  // namespace agent
}  // namespace agentmanager
