#pragma once

#include "algorithm_support/algorithm_data.h"
#include "algorithm_support/algorithm_types.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm_management {

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

struct AlgorithmPipelineStageSubmission {
  std::string stage_name;
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
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
  Cpu = 0,
  Gpu = 1,
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
  Body = 0,
  PreExecution = 1,
  PostExecution = 2,
  ResultRender = 3,
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

struct CpuPipelineRegistration {
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

struct CpuPendingPipelineStage0Submission {
  std::string owner_agent_name{};
  uint64_t lane_id{0u};
  bool loop_lane_active{false};
  std::shared_ptr<algorithm::AlgorithmContainerSet> prepared_container_set{};
  std::vector<AlgorithmResourceBinding> resource_bindings;
  std::vector<AlgorithmDescriptorValue> descriptor_values;
};

struct CpuPipelineInterStageBufferRuntimeState {
  std::string standard_container_slot_name{};
  uint32_t scalar_slot_count{0u};
  std::vector<float> scalar_slots{};
  bool valid{false};
};

struct CpuPipelineLaneRuntimeState {
  std::string owner_agent_name{};
  uint64_t lane_id{0u};
  bool loop_lane_active{false};
  std::shared_ptr<algorithm::AlgorithmContainerSet> standard_container_set{};
  std::vector<AlgorithmResourceBinding> resource_bindings{};
  std::vector<AlgorithmDescriptorValue> descriptor_values{};
  std::vector<bool> stage_has_data{};
  CpuPipelineInterStageBufferRuntimeState inter_stage_buffer{};
  bool valid{false};
};

struct CpuPipelineRuntimeState {
  std::string owner_agent_name{};
  AlgorithmPipelineTopology topology{AlgorithmPipelineTopology::NonCircular};
  AlgorithmPipelineSyncMode sync_mode{AlgorithmPipelineSyncMode::Forced};
  uint32_t max_concurrent_stage0_submissions{0u};
  uint64_t next_lane_id{1u};
  uint64_t current_lane_id{0u};
  std::string mandatory_stage_buffer_slot_name{};
  std::vector<CpuPipelineLaneRuntimeState> lanes{};
  std::vector<bool> stage_has_data{};
  std::vector<CpuPendingPipelineStage0Submission> pending_stage0_submissions{};
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

struct AlgorithmGpuExecContainerBinding {
  std::string container_name;
  std::string container_kind;
  uint32_t tuple_width{0u};
  bool required{true};
};

struct AlgorithmGpuExecShaderSpec {
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::string pipeline_kind;
};

struct AlgorithmGpuExecSpec {
  std::string stage_name{"exec"};
  std::vector<std::string> functions;
  std::vector<AlgorithmGpuExecContainerBinding> used_algorithm_containers;
  AlgorithmGpuExecShaderSpec shader;
};

struct AgentTickContext {
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  float dt_seconds{0.0f};
  AlgorithmExecutionPhase execution_phase{AlgorithmExecutionPhase::Body};
  AlgorithmJobPriority job_priority{AlgorithmJobPriority::High};
  const InteractionInterventionRequest* intervention_request{nullptr};
};

struct AgentAlgorithmRuntimeState {
  std::string algorithm_name;
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  AlgorithmPackageDebugState debug_state{};
  AlgorithmReflectionSnapshot reflection_snapshot{};
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
  float pipeline_total_elapsed_seconds{0.0f};
  std::vector<AlgorithmPipelineStageRuntimeStat> pipeline_stage_runtime_stats;
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
class IAlgorithmCpuExecutor;
class IAlgorithmGpuExecutor;
class IAlgorithmIntervention;
class AlgorithmObject;

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
  bool cpu_symbol{true};
  bool gpu_symbol{true};
  AlgorithmMountMode mount_mode{AlgorithmMountMode::Direct};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Gpu};
  AlgorithmTickLifetime tick_lifetime{AlgorithmTickLifetime::Continuous};
  std::shared_ptr<IAlgorithmGpuExecutor> gpu_executor;
  std::shared_ptr<IAlgorithmCpuExecutor> cpu_executor;
  std::shared_ptr<IAlgorithmIntervention> intervention;

 private:
  void EnsureContainerSet() {
    if (!shared_container_set) {
      shared_container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
    }
  }
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

class IAlgorithmCpuExecutor {
 public:
  virtual ~IAlgorithmCpuExecutor() = default;

  virtual bool ExecuteCpuAlgorithm(
    const AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    AlgorithmPackageDebugState* debug_state) = 0;
};

class IAlgorithmGpuExecutor {
 public:
  virtual ~IAlgorithmGpuExecutor() = default;

  virtual bool GetGpuExecSpec(AlgorithmGpuExecSpec* out_spec) const = 0;
};

enum class AlgorithmInterventionStageKind {
  ResultRender = 0,
  PreExecution = 1,
  InExecution = 2,
  PostExecution = 3,
  Custom = 4,
};

struct AlgorithmInterventionContainerBinding {
  std::string container_name;
  std::string container_kind;
  uint32_t tuple_width{0u};
  bool required{true};
};

struct AlgorithmInterventionShaderSpec {
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::string pipeline_kind;
};

struct AlgorithmInterventionStageSpec {
  std::string stage_name;
  AlgorithmInterventionStageKind stage_kind{AlgorithmInterventionStageKind::Custom};
  std::vector<std::string> functions;
  std::vector<AlgorithmInterventionContainerBinding> used_algorithm_containers;
  AlgorithmInterventionShaderSpec shader;
};

class IAlgorithmIntervention {
 public:
  virtual ~IAlgorithmIntervention() = default;

  virtual bool SupportsIntervention() const = 0;
  virtual void FillAgentToAlgorithmSignal(
    const AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const = 0;
  virtual bool GetInterventionStageSpecs(
    std::vector<AlgorithmInterventionStageSpec>* out_stage_specs) const = 0;
};

}  // namespace algorithm_management

namespace algorithmManager {
using ::algorithm_management::AgentAlgorithmRuntimeState;
using ::algorithm_management::AgentInitConfig;
using ::algorithm_management::AgentTickContext;
using ::algorithm_management::AgentTickResult;
using ::algorithm_management::AlgorithmAssemblySlot;
using ::algorithm_management::AlgorithmAssemblyState;
using ::algorithm_management::AlgorithmDescriptorValue;
using ::algorithm_management::AlgorithmExecutionPhase;
using ::algorithm_management::AlgorithmExecutionPreference;
using ::algorithm_management::AlgorithmPipelineSubmissionMode;
using ::algorithm_management::AlgorithmPipelineTopology;
using ::algorithm_management::AlgorithmPipelineSyncMode;
using ::algorithm_management::AlgorithmPipelineWrapperRole;
using ::algorithm_management::CpuPipelineInterStageBufferRuntimeState;
using ::algorithm_management::CpuPipelineRegistration;
using ::algorithm_management::CpuPipelineRuntimeState;
using ::algorithm_management::AlgorithmJobPriority;
using ::algorithm_management::AlgorithmInterventionContainerBinding;
using ::algorithm_management::AlgorithmInterventionPackageDebugState;
using ::algorithm_management::AlgorithmInterventionShaderSpec;
using ::algorithm_management::AlgorithmInterventionStageKind;
using ::algorithm_management::AlgorithmInterventionStageSpec;
using ::algorithm_management::AlgorithmGpuExecContainerBinding;
using ::algorithm_management::AlgorithmGpuExecShaderSpec;
using ::algorithm_management::AlgorithmGpuExecSpec;
using ::algorithm_management::AlgorithmMountMode;
using ::algorithm_management::AlgorithmPipelineStageSubmission;
using ::algorithm_management::AlgorithmPipelineStageRuntimeStat;
using ::algorithm_management::AlgorithmObject;
using ::algorithm_management::AlgorithmPackageDebugState;
using ::algorithm_management::PipelineStageBridgeDebugBinding;
using ::algorithm_management::PipelineStageBridgeDebugSet;
using ::algorithm_management::AlgorithmReflectionSnapshot;
using ::algorithm_management::AlgorithmReflectionValue;
using ::algorithm_management::AlgorithmTickLifetime;
using ::algorithm_management::AlgorithmRequestedDescriptorBindings;
using ::algorithm_management::AlgorithmRequestedResources;
using ::algorithm_management::AlgorithmResourceBinding;
using ::algorithm_management::IAlgorithmIntervention;
using ::algorithm_management::IAlgorithmPackageSupport;
using ::algorithm_management::IAlgorithmCpuExecutor;
using ::algorithm_management::IAlgorithmGpuExecutor;
using ::algorithm_management::IComplexAlgorithmPackageSupport;
using ::algorithm_management::ISimpleAlgorithmPackageSupport;
namespace support {}
namespace scheduler {}
}  // namespace algorithmManager

namespace agent {
using algorithmManager::AgentAlgorithmRuntimeState;
using algorithmManager::AgentInitConfig;
using algorithmManager::AgentTickContext;
using algorithmManager::AgentTickResult;
using algorithmManager::AlgorithmAssemblySlot;
using algorithmManager::AlgorithmAssemblyState;
using algorithmManager::AlgorithmDescriptorValue;
using algorithmManager::AlgorithmExecutionPhase;
using algorithmManager::AlgorithmExecutionPreference;
using algorithmManager::AlgorithmPipelineSubmissionMode;
using algorithmManager::AlgorithmPipelineTopology;
using algorithmManager::AlgorithmPipelineSyncMode;
using algorithmManager::CpuPipelineInterStageBufferRuntimeState;
using algorithmManager::CpuPipelineRegistration;
using algorithmManager::CpuPipelineRuntimeState;
using algorithmManager::AlgorithmJobPriority;
using algorithmManager::AlgorithmInterventionContainerBinding;
using algorithmManager::AlgorithmInterventionPackageDebugState;
using algorithmManager::AlgorithmInterventionShaderSpec;
using algorithmManager::AlgorithmInterventionStageKind;
using algorithmManager::AlgorithmInterventionStageSpec;
using algorithmManager::AlgorithmGpuExecContainerBinding;
using algorithmManager::AlgorithmGpuExecShaderSpec;
using algorithmManager::AlgorithmGpuExecSpec;
using algorithmManager::AlgorithmMountMode;
using algorithmManager::AlgorithmPipelineStageSubmission;
using algorithmManager::AlgorithmPipelineStageRuntimeStat;
using algorithmManager::AlgorithmObject;
using algorithmManager::AlgorithmPackageDebugState;
using algorithmManager::PipelineStageBridgeDebugBinding;
using algorithmManager::PipelineStageBridgeDebugSet;
using algorithmManager::AlgorithmReflectionSnapshot;
using algorithmManager::AlgorithmReflectionValue;
using algorithmManager::AlgorithmTickLifetime;
using algorithmManager::AlgorithmRequestedDescriptorBindings;
using algorithmManager::AlgorithmRequestedResources;
using algorithmManager::AlgorithmResourceBinding;
using algorithmManager::IAlgorithmIntervention;
using algorithmManager::IAlgorithmPackageSupport;
using algorithmManager::IAlgorithmCpuExecutor;
using algorithmManager::IAlgorithmGpuExecutor;
using algorithmManager::IComplexAlgorithmPackageSupport;
using algorithmManager::ISimpleAlgorithmPackageSupport;
}  // namespace agent
