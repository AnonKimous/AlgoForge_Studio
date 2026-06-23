#include "agent/agent.h"

#include "algorithm_management/algorithm_manager.h"
#include "common_data/kernel_cfg.h"

#include <chrono>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace agent {

namespace {

using PendingPipelineStage0Submission = algorithm_management::CpuPendingPipelineStage0Submission;
using PipelineLaneRuntimeState = algorithm_management::CpuPipelineLaneRuntimeState;
using PipelineRuntimeState = algorithm_management::CpuPipelineRuntimeState;

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

constexpr float kPipelineNoProgressStallSeconds = 1.0f;
constexpr uint64_t kFnvOffsetBasis64 = 1469598103934665603ull;
constexpr uint64_t kFnvPrime64 = 1099511628211ull;

uint64_t _HashBytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= kFnvPrime64;
  }
  return hash;
}

uint64_t _HashString(uint64_t hash, const std::string& value) {
  hash = _HashBytes(hash, value.data(), value.size());
  const char terminator = '\0';
  return _HashBytes(hash, &terminator, sizeof(terminator));
}

uint64_t _HashU64(uint64_t hash, uint64_t value) {
  return _HashBytes(hash, &value, sizeof(value));
}

uint64_t _HashBool(uint64_t hash, bool value) {
  const uint8_t byte_value = value ? 1u : 0u;
  return _HashBytes(hash, &byte_value, sizeof(byte_value));
}

uint64_t _HashSignal(uint64_t hash, const AlgorithmToAgentSignal& signal) {
  hash = _HashBool(hash, signal.intervention_applied);
  hash = _HashBool(hash, signal.pause_requested);
  hash = _HashBool(hash, signal.stop_requested);
  hash = _HashBool(hash, signal.intervention_needed);
  hash = _HashBool(hash, signal.reflection_collection_requested);
  hash = _HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

uint64_t _HashSignal(uint64_t hash, const AgentToAlgorithmSignal& signal) {
  hash = _HashBool(hash, signal.needs_intervention);
  hash = _HashBool(hash, signal.pause_requested);
  hash = _HashBool(hash, signal.stop_requested);
  hash = _HashBool(hash, signal.reflection_collection_requested);
  hash = _HashU64(hash, static_cast<uint64_t>(signal.control_bits));
  return hash;
}

uint64_t _HashContainer(uint64_t hash, const algorithm::AlgorithmContainer& container) {
  hash = _HashString(hash, container.name);
  hash = _HashU64(hash, static_cast<uint64_t>(container.storage_kind));
  hash = _HashU64(hash, static_cast<uint64_t>(container.element_count));
  hash = _HashU64(hash, static_cast<uint64_t>(container.element_stride));
  hash = _HashBool(hash, container.hidden);
  hash = _HashU64(hash, static_cast<uint64_t>(container.bytes.size()));
  if (!container.bytes.empty()) {
    hash = _HashBytes(hash, container.bytes.data(), container.bytes.size());
  }
  return hash;
}

bool _ShouldEmitPipelineRunnerProbe(const std::string& pipeline_name) {
  return pipeline_name.find("::runner_mount") != std::string::npos;
}

void _AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line) {
  const std::filesystem::path path =
    std::filesystem::path("D:/gptsandbox/artifacts/pipeline_runner") / file_name;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

uint64_t _HashContainerSet(uint64_t hash, const algorithm::AlgorithmContainerSet& container_set) {
  hash = _HashString(hash, container_set.algorithm_name);
  hash = _HashString(hash, container_set.standard_layout.layout_name);
  hash = _HashString(hash, container_set.standard_layout.layout_kind);
  hash = _HashU64(hash, static_cast<uint64_t>(container_set.standard_layout.variable_count));
  hash = _HashU64(hash, static_cast<uint64_t>(container_set.standard_layout.array_count));
  hash = _HashString(hash, container_set.standard_layout.variable_prefix);
  hash = _HashString(hash, container_set.standard_layout.array_prefix);
  for (const auto& container : container_set.arrays) {
    hash = _HashContainer(hash, container);
  }
  for (const auto& container : container_set.temporary_registers) {
    hash = _HashContainer(hash, container);
  }
  for (const auto& container : container_set.temporary_caches) {
    hash = _HashContainer(hash, container);
  }
  for (const auto& container : container_set.hidden_containers) {
    hash = _HashContainer(hash, container);
  }
  return hash;
}

uint64_t _HashPipelineGroupState(
  const std::vector<AlgorithmObject>& algorithm_objects,
  const std::vector<size_t>& executable_indices,
  uint64_t current_lane_id,
  size_t valid_lane_count,
  const std::vector<bool>& stage_has_data,
  size_t pending_stage0_submission_count,
  bool stage0_saturated,
  const std::vector<AgentAlgorithmRuntimeState>& runtime_states,
  size_t begin_index,
  size_t end_index) {
  uint64_t hash = kFnvOffsetBasis64;
  for (size_t index = begin_index; index < end_index; ++index) {
    const AlgorithmObject& object = algorithm_objects[index];
    hash = _HashString(hash, object.algorithm_profile.algorithm_name);
    hash = _HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_index));
    hash = _HashU64(hash, static_cast<uint64_t>(object.pipeline_stage_count));
    const algorithm::AlgorithmContainerSet* container_set = object.container_set();
    if (container_set) {
      hash = _HashContainerSet(hash, *container_set);
    }
    if (index < runtime_states.size()) {
      hash = _HashSignal(hash, runtime_states[index].agent_to_algorithm_signal);
      hash = _HashSignal(hash, runtime_states[index].algorithm_to_agent_signal);
    }
  }
  for (bool has_data : stage_has_data) {
    hash = _HashBool(hash, has_data);
  }
  hash = _HashU64(hash, current_lane_id);
  hash = _HashU64(hash, static_cast<uint64_t>(valid_lane_count));
  hash = _HashU64(hash, static_cast<uint64_t>(pending_stage0_submission_count));
  hash = _HashBool(hash, stage0_saturated);
  for (size_t index : executable_indices) {
    hash = _HashU64(hash, static_cast<uint64_t>(index));
  }
  return hash;
}

struct PipelineGroupProgressState {
  uint64_t signature{0u};
  bool signature_valid{false};
  float no_progress_seconds{0.0f};
  bool stall_report_requested{false};
  bool stall_reported{false};
  std::string stall_reason;
  float total_elapsed_seconds{0.0f};
  std::vector<AlgorithmPipelineStageRuntimeStat> stage_runtime_stats;
};

AlgorithmPipelineStageRuntimeStat* _FindPipelineStageRuntimeStat(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name) {
  if (!stage_runtime_stats) {
    return nullptr;
  }
  for (AlgorithmPipelineStageRuntimeStat& stage_stat : *stage_runtime_stats) {
    if (stage_stat.stage_name == stage_name) {
      return &stage_stat;
    }
  }
  return nullptr;
}

void _SetPipelineStageRuntimeReason(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  std::string reason) {
  AlgorithmPipelineStageRuntimeStat* stage_stat =
    _FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->reason = std::move(reason);
}

void _AddPipelineStageRuntimeElapsed(
  std::vector<AlgorithmPipelineStageRuntimeStat>* stage_runtime_stats,
  const std::string& stage_name,
  float elapsed_seconds) {
  AlgorithmPipelineStageRuntimeStat* stage_stat =
    _FindPipelineStageRuntimeStat(stage_runtime_stats, stage_name);
  if (!stage_stat) {
    return;
  }
  stage_stat->elapsed_seconds += elapsed_seconds;
}

std::string _BuildPipelineStageIdleReason(
  bool allow_tick,
  bool is_ready,
  bool launch_once_completed) {
  if (!allow_tick) {
    return "Tick was skipped by the scheduler.";
  }
  if (!is_ready) {
    return "Stage assembly is not ready.";
  }
  if (launch_once_completed) {
    return "Launch-once stage is holding its completed state.";
  }
  return "Stage did not execute.";
}

std::string _BuildPipelineStageNoInputReason(bool is_stage0, bool has_pending_stage0_submission) {
  if (is_stage0 && has_pending_stage0_submission) {
    return "Stage0 is waiting for a free execution slot before accepting the next resource batch.";
  }
  if (is_stage0) {
    return "Stage0 is waiting for an external resource batch.";
  }
  return "Stage has no pipeline input token.";
}

void _CollectDebugState(
  const AlgorithmObject& object,
  AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }

  if (auto* complex_support = dynamic_cast<IComplexAlgorithmPackageSupport*>(object.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

bool _CollectReflectionSnapshot(
  const AlgorithmObject& object,
  const AlgorithmContainerSet& container_set,
  AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }

  out_snapshot->Clear();
  out_snapshot->algorithm_name = object.algorithm_profile.algorithm_name;
  if (!object.algorithm_reflector) {
    return false;
  }

  for (const auto& [reflection_object_name, binding] : object.algorithm_reflector->container_bindings_by_reflection_object_name) {
    for (const std::string& container_name : binding.container_names) {
      const AlgorithmContainer* container = FindAlgorithmContainer(container_set, container_name);
      if (!container) {
        continue;
      }

      AlgorithmReflectionValue value{};
      value.reflection_object_name = reflection_object_name;
      value.container_name = container_name;
      value.filter_name = binding.filter_name;
      value.storage_kind = container->storage_kind;
      value.bytes.assign(container->bytes.begin(), container->bytes.end());

      if (container->storage_kind == algorithm::AlgorithmContainerStorageKind::Array) {
        out_snapshot->variable_arrays.push_back(std::move(value));
      } else {
        out_snapshot->variables.push_back(std::move(value));
      }
    }
  }

  out_snapshot->valid = !out_snapshot->variables.empty() || !out_snapshot->variable_arrays.empty();
  return out_snapshot->valid;
}

bool _CollectPipelineExitReflectionSnapshot(
  const std::string& algorithm_name,
  const AlgorithmContainerSet& container_set,
  AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }

  out_snapshot->Clear();
  out_snapshot->algorithm_name = algorithm_name.empty() ? container_set.algorithm_name : algorithm_name;

  for (const AlgorithmContainer& container : container_set.arrays) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variable_arrays.push_back(std::move(value));
  }
  for (const AlgorithmContainer& container : container_set.temporary_registers) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  for (const AlgorithmContainer& container : container_set.temporary_caches) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }
  for (const AlgorithmContainer& container : container_set.hidden_containers) {
    AlgorithmReflectionValue value{};
    value.reflection_object_name = container.name;
    value.container_name = container.name;
    value.filter_name = "pipeline_exit";
    value.storage_kind = container.storage_kind;
    value.bytes.assign(container.bytes.begin(), container.bytes.end());
    out_snapshot->variables.push_back(std::move(value));
  }

  out_snapshot->valid = !out_snapshot->variables.empty() || !out_snapshot->variable_arrays.empty();
  return out_snapshot->valid;
}

bool _SignalBlocksTick(
  const AlgorithmObject& object,
  const AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !object.intervention || object.intervention->SupportsIntervention();
}

bool _IsPipelineStage(const AlgorithmObject& object) {
  return object.pipeline_stage;
}

bool _FindPipelineGroupRange(
  const std::vector<AlgorithmObject>& algorithm_objects,
  size_t index,
  size_t* out_begin_index,
  size_t* out_end_index) {
  if (index >= algorithm_objects.size()) {
    return false;
  }
  const AlgorithmObject& anchor = algorithm_objects[index];
  if (!_IsPipelineStage(anchor)) {
    return false;
  }

  size_t begin_index = index;
  while (begin_index > 0u) {
    const AlgorithmObject& previous = algorithm_objects[begin_index - 1u];
    if (!_IsPipelineStage(previous) ||
        previous.pipeline_name != anchor.pipeline_name ||
        previous.pipeline_stage_index + 1u != algorithm_objects[begin_index].pipeline_stage_index) {
      break;
    }
    --begin_index;
  }

  size_t end_index = index + 1u;
  while (end_index < algorithm_objects.size()) {
    const AlgorithmObject& next = algorithm_objects[end_index];
    if (!_IsPipelineStage(next) ||
        next.pipeline_name != anchor.pipeline_name ||
        algorithm_objects[end_index - 1u].pipeline_stage_index + 1u != next.pipeline_stage_index) {
      break;
    }
    ++end_index;
  }

  if (out_begin_index) {
    *out_begin_index = begin_index;
  }
  if (out_end_index) {
    *out_end_index = end_index;
  }
  return true;
}

bool _CopyPipelineCircularLoopback(
  const algorithm::AlgorithmContainerSet& source_container_set,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  AlgorithmObject* target_stage,
  std::string* out_error_message) {
  if (!target_stage || !target_stage->mutable_container_set()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback target container set is unavailable.";
    }
    return false;
  }

  algorithm::AlgorithmContainerSet* target_container_set = target_stage->mutable_container_set();
  if (!source_container_set.standard_layout.enabled() ||
      !target_container_set->standard_layout.enabled()) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback requires standard container layouts.";
    }
    return false;
  }
  if (source_container_set.standard_layout.variable_count < shared_variable_count ||
      target_container_set->standard_layout.variable_count < shared_variable_count ||
      source_container_set.standard_layout.array_count < shared_array_count ||
      target_container_set->standard_layout.array_count < shared_array_count) {
    if (out_error_message) {
      *out_error_message = "Circular pipeline loopback shared prefix is out of range.";
    }
    return false;
  }

  for (uint32_t i = 0; i < shared_variable_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeVariableName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback is missing standard variable slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback variable slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }

  for (uint32_t i = 0; i < shared_array_count; ++i) {
    const std::string slot_name = source_container_set.standard_layout.MakeArrayName(i);
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, slot_name);
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, slot_name);
    if (!source_container || !target_container) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback is missing standard array slot '" + slot_name + "'.";
      }
      return false;
    }
    if (!algorithm::HasSameContainerStructure(*source_container, *target_container)) {
      if (out_error_message) {
        *out_error_message = "Circular pipeline loopback array slot structure mismatch at '" + slot_name + "'.";
      }
      return false;
    }
    std::memcpy(target_container->bytes.data(), source_container->bytes.data(), source_container->bytes.size());
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _HasPipelineStageBufferSlot(
  const algorithm::AlgorithmContainerSet& container_set,
  const std::string& stage_buffer_slot_name) {
  if (stage_buffer_slot_name.empty()) {
    return false;
  }
  const algorithm::AlgorithmContainer* stage_buffer_container =
    algorithm::FindAlgorithmContainer(container_set, stage_buffer_slot_name);
  return stage_buffer_container != nullptr &&
    algorithm::IsStandardContainerSlotName(container_set, stage_buffer_slot_name);
}

bool _PreparePipelineStage0Submission(
  const std::string& stage0_algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  const std::string& stage_buffer_slot_name,
  PendingPipelineStage0Submission* out_submission,
  std::string* out_error_message) {
  if (!out_submission) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage0 submission output pointer is null.";
    }
    return false;
  }

  AlgorithmObject prepared_object{};
  if (!algorithm_management::PrepareAlgorithmObjectByName(
        stage0_algorithm_name,
        resource_bindings,
        descriptor_values,
        &prepared_object,
        out_error_message)) {
    return false;
  }
  if (!_HasPipelineStageBufferSlot(*prepared_object.mutable_container_set(), stage_buffer_slot_name)) {
    if (out_error_message) {
      *out_error_message =
        "Prepared stage0 submission is missing the required pipeline stage buffer slot '" +
        stage_buffer_slot_name + "'.";
    }
    return false;
  }

  out_submission->prepared_container_set = prepared_object.shared_container_set;
  out_submission->resource_bindings = resource_bindings;
  out_submission->descriptor_values = descriptor_values;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

void _UpdatePipelineGroupProgressState(
  const PipelineGroupProgressState& previous_state,
  uint64_t current_signature,
  bool signature_valid,
  float dt_seconds,
  PipelineGroupProgressState* out_state) {
  if (!out_state) {
    return;
  }
  *out_state = previous_state;
  out_state->total_elapsed_seconds = previous_state.total_elapsed_seconds + dt_seconds;
  out_state->stall_report_requested = false;
  if (!signature_valid) {
    return;
  }

  if (previous_state.signature_valid && previous_state.signature == current_signature) {
    out_state->signature = current_signature;
    out_state->signature_valid = true;
    out_state->no_progress_seconds = previous_state.no_progress_seconds + dt_seconds;
    if (!previous_state.stall_reported && out_state->no_progress_seconds >= kPipelineNoProgressStallSeconds) {
      out_state->stall_report_requested = true;
      out_state->stall_reported = true;
    }
    return;
  }

  out_state->signature = current_signature;
  out_state->signature_valid = true;
  out_state->no_progress_seconds = 0.0f;
  out_state->stall_reported = false;
  out_state->stall_reason.clear();
}

void _ResetRuntimeStateBase(
  const AgentAlgorithmRuntimeState& previous_runtime_state,
  AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return;
  }
  *runtime_state = previous_runtime_state;
  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};
  runtime_state->bridge_debug_set.Clear();
  if (!runtime_state->reflection_snapshot_cached) {
    runtime_state->reflection_snapshot.Clear();
  }
}

bool _PipelineGroupIsExecutable(
  const std::vector<size_t>& executable_indices) {
  return !executable_indices.empty();
}

size_t _CountLivePipelineStages(const std::vector<bool>& stage_has_data) {
  size_t live_stage_count = 0u;
  for (bool has_data : stage_has_data) {
    if (has_data) {
      ++live_stage_count;
    }
  }
  return live_stage_count;
}

size_t _CountValidPipelineLanes(
  const PipelineRuntimeState& pipeline_state) {
  size_t valid_lane_count = 0u;
  for (const PipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
    if (lane_state.valid) {
      ++valid_lane_count;
    }
  }
  return valid_lane_count;
}

bool _TryBuildInitialPipelineLaneRuntimeState(
  const AlgorithmObject& stage0_object,
  size_t pipeline_stage_count,
  bool loop_lane_active,
  uint64_t lane_id,
  const std::string& stage_buffer_slot_name,
  PipelineLaneRuntimeState* out_lane_state,
  std::string* out_error_message) {
  if (!out_lane_state) {
    if (out_error_message) {
      *out_error_message = "Pipeline lane runtime state output pointer is null.";
    }
    return false;
  }
  const algorithm::AlgorithmContainerSet* standard_container_set = stage0_object.container_set();
  if (!standard_container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage0 standard container set is unavailable.";
    }
    return false;
  }
  if (!_HasPipelineStageBufferSlot(*standard_container_set, stage_buffer_slot_name)) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline stage0 standard container is missing the required pipeline stage buffer slot '" +
        stage_buffer_slot_name + "'.";
    }
    return false;
  }

  out_lane_state->lane_id = lane_id;
  out_lane_state->loop_lane_active = loop_lane_active;
  out_lane_state->standard_container_set = stage0_object.shared_container_set;
  out_lane_state->resource_bindings = stage0_object.resource_bindings;
  out_lane_state->descriptor_values = stage0_object.descriptor_values;
  out_lane_state->stage_has_data.assign(pipeline_stage_count, false);
  out_lane_state->inter_stage_buffer.standard_container_slot_name = stage_buffer_slot_name;
  out_lane_state->inter_stage_buffer.valid = true;
  out_lane_state->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

PipelineLaneRuntimeState* _FindPrimaryPipelineLaneRuntimeState(
  PipelineRuntimeState* pipeline_state) {
  if (!pipeline_state) {
    return nullptr;
  }
  if (pipeline_state->current_lane_id != 0u) {
    for (PipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
      if (lane_state.valid && lane_state.lane_id == pipeline_state->current_lane_id) {
        return &lane_state;
      }
    }
  }
  for (PipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
    if (lane_state.valid) {
      return &lane_state;
    }
  }
  return nullptr;
}

const PipelineLaneRuntimeState* _FindPrimaryPipelineLaneRuntimeState(
  const PipelineRuntimeState& pipeline_state) {
  if (pipeline_state.current_lane_id != 0u) {
    for (const PipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
      if (lane_state.valid && lane_state.lane_id == pipeline_state.current_lane_id) {
        return &lane_state;
      }
    }
  }
  for (const PipelineLaneRuntimeState& lane_state : pipeline_state.lanes) {
    if (lane_state.valid) {
      return &lane_state;
    }
  }
  return nullptr;
}

void _SyncPipelineLegacyStageStateFromPrimaryLane(
  PipelineRuntimeState* pipeline_state,
  size_t pipeline_stage_count) {
  if (!pipeline_state) {
    return;
  }
  pipeline_state->stage_has_data.assign(pipeline_stage_count, false);
  const PipelineLaneRuntimeState* primary_lane =
    _FindPrimaryPipelineLaneRuntimeState(*pipeline_state);
  if (!primary_lane) {
    return;
  }
  pipeline_state->stage_has_data = primary_lane->stage_has_data;
}

void _CommitPipelineStageStateToPrimaryLane(
  PipelineRuntimeState* pipeline_state,
  const std::vector<bool>& next_stage_has_data) {
  if (!pipeline_state) {
    return;
  }
  PipelineLaneRuntimeState* primary_lane =
    _FindPrimaryPipelineLaneRuntimeState(pipeline_state);
  if (primary_lane) {
    primary_lane->stage_has_data = next_stage_has_data;
    if (!next_stage_has_data.empty()) {
      primary_lane->loop_lane_active = next_stage_has_data.front();
    }
  }
  pipeline_state->stage_has_data = next_stage_has_data;
}

PipelineLaneRuntimeState* _FindPipelineLaneRuntimeStateById(
  PipelineRuntimeState* pipeline_state,
  uint64_t lane_id) {
  if (!pipeline_state || lane_id == 0u) {
    return nullptr;
  }
  for (PipelineLaneRuntimeState& lane_state : pipeline_state->lanes) {
    if (lane_state.valid && lane_state.lane_id == lane_id) {
      return &lane_state;
    }
  }
  return nullptr;
}

void _MergeAlgorithmToAgentSignal(
  const AlgorithmToAgentSignal& source_signal,
  AlgorithmToAgentSignal* out_target_signal) {
  if (!out_target_signal) {
    return;
  }
  out_target_signal->intervention_applied = out_target_signal->intervention_applied || source_signal.intervention_applied;
  out_target_signal->pause_requested = out_target_signal->pause_requested || source_signal.pause_requested;
  out_target_signal->stop_requested = out_target_signal->stop_requested || source_signal.stop_requested;
  out_target_signal->intervention_needed = out_target_signal->intervention_needed || source_signal.intervention_needed;
  out_target_signal->reflection_collection_requested =
    out_target_signal->reflection_collection_requested || source_signal.reflection_collection_requested;
  out_target_signal->control_bits |= source_signal.control_bits;
}

void _AppendAlgorithmTimingLogLine(
  std::ostringstream* stream,
  const AlgorithmObject& object,
  bool executed,
  bool allow_tick,
  bool is_ready,
  float elapsed_seconds) {
  if (!stream) {
    return;
  }
  *stream << "algorithm " << object.algorithm_profile.algorithm_name
          << " | executed=" << (executed ? "true" : "false")
          << " | allow_tick=" << (allow_tick ? "true" : "false")
          << " | ready=" << (is_ready ? "true" : "false")
          << " | execution=" << (object.execution_preference == AlgorithmExecutionPreference::Cpu ? "cpu" : "gpu")
          << " | elapsed_seconds=" << elapsed_seconds << '\n';
}

void _AppendPipelineTimingLog(
  std::ostringstream* stream,
  const std::string& pipeline_name,
  const AgentAlgorithmRuntimeState& runtime_state) {
  if (!stream || runtime_state.pipeline_stage_runtime_stats.empty()) {
    return;
  }
  *stream << "pipeline " << pipeline_name
          << " | total_elapsed_seconds=" << runtime_state.pipeline_total_elapsed_seconds;
  if (runtime_state.pipeline_active_stage_index_valid) {
    *stream << " | active_stage_index=" << runtime_state.pipeline_active_stage_index;
  }
  *stream << '\n';
  for (const AlgorithmPipelineStageRuntimeStat& stage_stat : runtime_state.pipeline_stage_runtime_stats) {
    *stream << "  stage " << stage_stat.stage_name
            << " | elapsed_seconds=" << stage_stat.elapsed_seconds;
    if (!stage_stat.reason.empty()) {
      *stream << " | reason=" << stage_stat.reason;
    }
    *stream << '\n';
  }
}

bool _TickAlgorithmObject(
  AlgorithmObject& object,
  const AgentTickContext& context,
  bool allow_tick,
  bool is_ready,
  bool execute_now,
  AgentAlgorithmRuntimeState* runtime_state) {
  if (!runtime_state) {
    return false;
  }

  runtime_state->algorithm_to_agent_signal = {};
  runtime_state->debug_state = {};

  const bool launch_once_then_hold = object.tick_lifetime == AlgorithmTickLifetime::LaunchOnceThenHold;
  const bool launch_once_completed = launch_once_then_hold && runtime_state->launch_once_completed;
  const bool has_runtime_reflector = object.algorithm_reflector && !object.algorithm_reflector->empty();
  const bool capture_reflection_once =
    has_runtime_reflector &&
    object.algorithm_reflector->refresh_mode == algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
  const bool keep_cached_reflection = runtime_state->reflection_snapshot_cached;
  if (!keep_cached_reflection) {
    runtime_state->reflection_snapshot.Clear();
  }

  if (launch_once_completed) {
    runtime_state->agent_to_algorithm_signal = {};
    return true;
  }

  if (!execute_now) {
    if (is_ready) {
      runtime_state->algorithm_to_agent_signal.pause_requested =
        runtime_state->agent_to_algorithm_signal.pause_requested;
      runtime_state->algorithm_to_agent_signal.stop_requested =
        runtime_state->agent_to_algorithm_signal.stop_requested;
      runtime_state->algorithm_to_agent_signal.intervention_needed =
        _SignalBlocksTick(object, runtime_state->agent_to_algorithm_signal) &&
        runtime_state->agent_to_algorithm_signal.needs_intervention;
      runtime_state->algorithm_to_agent_signal.reflection_collection_requested =
        runtime_state->agent_to_algorithm_signal.reflection_collection_requested;
      runtime_state->algorithm_to_agent_signal.control_bits = runtime_state->agent_to_algorithm_signal.control_bits;
    } else {
      runtime_state->agent_to_algorithm_signal = {};
      runtime_state->algorithm_to_agent_signal = {};
    }
    (void)allow_tick;
    return true;
  }

  runtime_state->algorithm_to_agent_signal.intervention_needed =
    runtime_state->agent_to_algorithm_signal.needs_intervention &&
    object.intervention &&
    object.intervention->SupportsIntervention();
  runtime_state->algorithm_to_agent_signal.control_bits = runtime_state->agent_to_algorithm_signal.control_bits;

  std::string submit_error_message;
  if (!algorithm_management::SubmitAlgorithmObject(
        object,
        context,
        runtime_state->agent_to_algorithm_signal,
        object.mutable_container_set(),
        &runtime_state->algorithm_to_agent_signal,
        &runtime_state->debug_state,
        &submit_error_message)) {
    const std::string failure_message = submit_error_message.empty()
      ? std::string("Algorithm execution failed.")
      : std::move(submit_error_message);
    DEBUG_TOOL_ASSERT(false, failure_message.c_str());
    runtime_state->algorithm_to_agent_signal.stop_requested = true;
    return false;
  }

  AlgorithmPackageDebugState collected_debug_state{};
  _CollectDebugState(object, &collected_debug_state);
  runtime_state->debug_state.signals.insert(
    runtime_state->debug_state.signals.end(),
    collected_debug_state.signals.begin(),
    collected_debug_state.signals.end());

  if (has_runtime_reflector) {
    const bool should_collect_reflection =
      !capture_reflection_once || !runtime_state->reflection_snapshot_cached;
    if (should_collect_reflection) {
      if (!object.container_set()) {
        DEBUG_TOOL_ASSERT(false, "Runtime reflection snapshot requires a container set.");
        runtime_state->algorithm_to_agent_signal.stop_requested = true;
        return false;
      }
      if (!_CollectReflectionSnapshot(
            object,
            *object.container_set(),
            &runtime_state->reflection_snapshot)) {
        DEBUG_TOOL_ASSERT(false, "Runtime reflection snapshot could not be collected.");
        runtime_state->algorithm_to_agent_signal.stop_requested = true;
        return false;
      }
      runtime_state->algorithm_to_agent_signal.reflection_collection_requested = true;
      if (capture_reflection_once) {
        runtime_state->reflection_snapshot_cached = true;
      }
    }
  } else if (runtime_state->agent_to_algorithm_signal.reflection_collection_requested) {
    DEBUG_TOOL_ASSERT(false, "Reflection was requested but the algorithm has no runtime reflector.");
    runtime_state->algorithm_to_agent_signal.stop_requested = true;
    return false;
  }

  runtime_state->algorithm_to_agent_signal.intervention_applied =
    runtime_state->algorithm_to_agent_signal.intervention_applied ||
    runtime_state->algorithm_to_agent_signal.intervention_needed;
  if (!runtime_state->algorithm_to_agent_signal.stop_requested && launch_once_then_hold) {
    runtime_state->launch_once_completed = true;
    if (!has_runtime_reflector) {
      runtime_state->reflection_snapshot_cached = true;
    }
  }

  return true;
}

bool _BuildPipelineStageContainerSets(
  const std::vector<AlgorithmObject>& algorithm_objects,
  size_t begin_index,
  size_t end_index,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* out_stage_container_sets,
  std::string* out_error_message) {
  if (!out_stage_container_sets) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage container set output pointer is null.";
    }
    return false;
  }
  out_stage_container_sets->clear();
  out_stage_container_sets->reserve(end_index > begin_index ? end_index - begin_index : 0u);

  for (size_t index = begin_index; index < end_index; ++index) {
    const AlgorithmObject& object = algorithm_objects[index];
    if (object.algorithm_profile.algorithm_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is empty.";
      }
      return false;
    }
    if (!object.container_set()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage container set is unavailable for '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
    const auto [_, inserted] = out_stage_container_sets->emplace(
      object.algorithm_profile.algorithm_name,
      object.shared_container_set);
    if (!inserted) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage name is duplicated: '" + object.algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

struct BuiltAlgorithmMount {
  AlgorithmObject object{};
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set{};
  std::string error_message;
  bool ok{false};
};

bool _RuntimeTransferMapsMatch(
  const algorithm::AlgorithmRuntimeTransferMap& expected_map,
  const algorithm::AlgorithmRuntimeTransferMap& candidate_map,
  std::string* out_error_message) {
  if (expected_map.supports_circular_tick != candidate_map.supports_circular_tick) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage runtime transfer map circular tick support does not match.";
    }
    return false;
  }
  if (expected_map.stage_links.size() != candidate_map.stage_links.size()) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage runtime transfer map edge count does not match.";
    }
    return false;
  }
  for (size_t edge_index = 0u; edge_index < expected_map.stage_links.size(); ++edge_index) {
    const algorithm::AlgorithmRuntimeTransferEdge& expected_edge = expected_map.stage_links[edge_index];
    const algorithm::AlgorithmRuntimeTransferEdge& candidate_edge = candidate_map.stage_links[edge_index];
    if (expected_edge.source_stage_name != candidate_edge.source_stage_name ||
        expected_edge.target_stage_name != candidate_edge.target_stage_name) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage runtime transfer map topology does not match.";
      }
      return false;
    }
    if (expected_edge.bindings.size() != candidate_edge.bindings.size()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage runtime transfer map binding count does not match.";
      }
      return false;
    }
    for (size_t binding_index = 0u; binding_index < expected_edge.bindings.size(); ++binding_index) {
      const algorithm::AlgorithmRuntimeTransferBinding& expected_binding =
        expected_edge.bindings[binding_index];
      const algorithm::AlgorithmRuntimeTransferBinding& candidate_binding =
        candidate_edge.bindings[binding_index];
      if (expected_binding.from_name != candidate_binding.from_name ||
          expected_binding.to_name != candidate_binding.to_name ||
          expected_binding.required != candidate_binding.required) {
        if (out_error_message) {
          *out_error_message = "Pipeline stage runtime transfer map binding content does not match.";
        }
        return false;
      }
    }
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryValidatePipelineSharedStandardPrefix(
  const std::vector<BuiltAlgorithmMount>& built_stages,
  uint32_t shared_variable_count,
  uint32_t shared_array_count,
  std::string* out_error_message) {
  if (built_stages.empty() || !built_stages.front().container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage container set list is empty.";
    }
    return false;
  }

  const algorithm::AlgorithmContainerSet& reference_container_set = *built_stages.front().container_set;
  for (size_t stage_index = 0u; stage_index < built_stages.size(); ++stage_index) {
    const BuiltAlgorithmMount& built_stage = built_stages[stage_index];
    if (!built_stage.container_set) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage container set is unavailable.";
      }
      return false;
    }

    const algorithm::AlgorithmContainerSet& candidate_container_set = *built_stage.container_set;
    for (uint32_t variable_index = 0u; variable_index < shared_variable_count; ++variable_index) {
      const std::string slot_name =
        reference_container_set.standard_layout.MakeVariableName(variable_index);
      const algorithm::AlgorithmContainer* reference_container =
        algorithm::FindAlgorithmContainer(reference_container_set, slot_name);
      const algorithm::AlgorithmContainer* candidate_container =
        algorithm::FindAlgorithmContainer(candidate_container_set, slot_name);
      if (!reference_container || !candidate_container) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard variable slot is missing: '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (!algorithm::HasSameContainerStructure(*reference_container, *candidate_container)) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard variable slot structure mismatch at '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
    }

    for (uint32_t array_index = 0u; array_index < shared_array_count; ++array_index) {
      const std::string slot_name =
        reference_container_set.standard_layout.MakeArrayName(array_index);
      const algorithm::AlgorithmContainer* reference_container =
        algorithm::FindAlgorithmContainer(reference_container_set, slot_name);
      const algorithm::AlgorithmContainer* candidate_container =
        algorithm::FindAlgorithmContainer(candidate_container_set, slot_name);
      if (!reference_container || !candidate_container) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard array slot is missing: '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (!algorithm::HasSameContainerStructure(*reference_container, *candidate_container)) {
        if (out_error_message) {
          *out_error_message = "Pipeline shared standard array slot structure mismatch at '" + slot_name +
            "' in stage '" + built_stage.object.algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryBuildMountedPipelineTransferMap(
  const std::vector<BuiltAlgorithmMount>& built_stages,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "Mounted pipeline runtime transfer map output pointer is null.";
    }
    return false;
  }
  out_transfer_map->reset();
  if (built_stages.empty()) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage mount list is empty.";
    }
    return false;
  }
  if (!built_stages.front().object.runtime_transfer_map ||
      !built_stages.front().object.runtime_transfer_map->valid) {
    if (out_error_message) {
      *out_error_message = "Pipeline stage runtime transfer map is unavailable on the first stage.";
    }
    return false;
  }

  algorithm::AlgorithmRuntimeTransferMap transfer_map =
    *built_stages.front().object.runtime_transfer_map;
  uint32_t shared_variable_count = UINT32_MAX;
  uint32_t shared_array_count = UINT32_MAX;
  uint32_t total_extra_variable_count = 0u;
  uint32_t total_extra_array_count = 0u;

  for (const BuiltAlgorithmMount& built_stage : built_stages) {
    if (!built_stage.container_set || !built_stage.container_set->standard_layout.enabled()) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage standard container layout is unavailable: " +
          built_stage.object.algorithm_profile.algorithm_name;
      }
      return false;
    }
    if (!built_stage.object.runtime_transfer_map || !built_stage.object.runtime_transfer_map->valid) {
      if (out_error_message) {
        *out_error_message = "Pipeline stage runtime transfer map is unavailable: " +
          built_stage.object.algorithm_profile.algorithm_name;
      }
      return false;
    }
    std::string map_compare_error;
    if (!_RuntimeTransferMapsMatch(
          transfer_map,
          *built_stage.object.runtime_transfer_map,
          &map_compare_error)) {
      if (out_error_message) {
        *out_error_message = map_compare_error.empty()
          ? ("Pipeline stage runtime transfer map mismatch: " +
             built_stage.object.algorithm_profile.algorithm_name)
          : (map_compare_error + " Stage: " + built_stage.object.algorithm_profile.algorithm_name);
      }
      return false;
    }

    shared_variable_count = std::min(
      shared_variable_count,
      built_stage.container_set->standard_layout.variable_count);
    shared_array_count = std::min(
      shared_array_count,
      built_stage.container_set->standard_layout.array_count);
  }

  if (shared_variable_count == UINT32_MAX) {
    shared_variable_count = 0u;
  }
  if (shared_array_count == UINT32_MAX) {
    shared_array_count = 0u;
  }
  if (shared_array_count == 0u) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline stages must share at least one standard array slot for the implicit stage buffer.";
    }
    return false;
  }

  std::string prefix_validation_error;
  if (!_TryValidatePipelineSharedStandardPrefix(
        built_stages,
        shared_variable_count,
        shared_array_count,
        &prefix_validation_error)) {
    if (out_error_message) {
      *out_error_message = prefix_validation_error;
    }
    return false;
  }

  transfer_map.stage_layouts.clear();
  transfer_map.stage_layouts.reserve(built_stages.size());
  for (const BuiltAlgorithmMount& built_stage : built_stages) {
    const algorithm::AlgorithmStandardContainerLayout& standard_layout =
      built_stage.container_set->standard_layout;
    const uint32_t extra_variable_count =
      standard_layout.variable_count - shared_variable_count;
    const uint32_t extra_array_count =
      standard_layout.array_count - shared_array_count;
    if (extra_array_count != 0u) {
      if (out_error_message) {
        *out_error_message =
          "Pipeline stage is not allowed to declare extra standard arrays beyond the shared prefix: " +
          built_stage.object.algorithm_profile.algorithm_name;
      }
      return false;
    }

    transfer_map.stage_layouts.push_back(algorithm::AlgorithmRuntimeTransferStageLayout{
      .stage_name = built_stage.object.algorithm_profile.algorithm_name,
      .declared_variable_count = standard_layout.variable_count,
      .declared_array_count = standard_layout.array_count,
      .shared_variable_count = shared_variable_count,
      .shared_array_count = shared_array_count,
      .extra_variable_count = extra_variable_count,
      .extra_array_count = extra_array_count,
      .extra_variable_offset = total_extra_variable_count,
      .extra_array_offset = total_extra_array_count,
    });
    total_extra_variable_count += extra_variable_count;
    total_extra_array_count += extra_array_count;
  }

  transfer_map.pipeline_shared_variable_count = shared_variable_count;
  transfer_map.pipeline_shared_array_count = shared_array_count;
  transfer_map.pipeline_total_extra_variable_count = total_extra_variable_count;
  transfer_map.pipeline_total_extra_array_count = total_extra_array_count;
  transfer_map.pipeline_shared_stage_buffer_slot_name =
    built_stages.front().container_set->standard_layout.MakeArrayName(shared_array_count - 1u);
  const algorithm::AlgorithmContainer* stage_buffer_container =
    algorithm::FindAlgorithmContainer(
      *built_stages.front().container_set,
      transfer_map.pipeline_shared_stage_buffer_slot_name);
  if (!stage_buffer_container) {
    if (out_error_message) {
      *out_error_message = "Pipeline shared stage buffer container is unavailable.";
    }
    return false;
  }
  if (stage_buffer_container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array ||
      stage_buffer_container->element_stride < sizeof(float) ||
      stage_buffer_container->element_count < total_extra_variable_count) {
    if (out_error_message) {
      *out_error_message =
        "Pipeline shared stage buffer does not have enough scalar capacity for all extra stage variables.";
    }
    return false;
  }

  *out_transfer_map = std::make_shared<algorithm::AlgorithmRuntimeTransferMap>(std::move(transfer_map));
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

std::string _StandardLayoutKey(const algorithm::AlgorithmContainerSet& container_set) {
  if (container_set.standard_layout.enabled()) {
    return container_set.standard_layout.layout_name;
  }
  return container_set.algorithm_name;
}

BuiltAlgorithmMount _BuildAlgorithmMount(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  AlgorithmMountMode mount_mode,
  AlgorithmExecutionPreference execution_preference,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets) {
  BuiltAlgorithmMount result{};
  const bool emit_runner_probe = _ShouldEmitPipelineRunnerProbe(algorithm_name) ||
    algorithm_name.find("v2a0_pipeline_square_vertex_demo") != std::string::npos;

  std::string error_message;
  AlgorithmObject object{};
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "build_mount.create_object.begin algorithm=" + algorithm_name);
  }
  if (!CreateAlgorithmObjectByName(algorithm_name, &object, &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to create algorithm object for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "build_mount.create_object.end algorithm=" + algorithm_name);
  }

  object.resource_bindings = resource_bindings;
  object.descriptor_values = descriptor_values;
  object.mount_mode = mount_mode;
  object.execution_preference = execution_preference;

  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set_handle = object.shared_container_set;
  if (!container_set_handle) {
    result.error_message = "Algorithm containers are unavailable for '" + algorithm_name + "'.";
    return result;
  }

  const bool use_shared_standard_container =
    mount_mode == AlgorithmMountMode::StandardContainer &&
    container_set_handle->standard_layout.enabled() &&
    standard_shared_container_sets;
  const bool cache_shared_standard_container = use_shared_standard_container;
  std::string shared_key;

  if (use_shared_standard_container) {
    shared_key = _StandardLayoutKey(*container_set_handle);
    if (!shared_key.empty()) {
      auto found = standard_shared_container_sets->find(shared_key);
      if (found != standard_shared_container_sets->end() && found->second) {
        container_set_handle = found->second;
      }
    }
  }

  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "build_mount.finalize.begin algorithm=" + algorithm_name);
  }
  if (!algorithm_management::FinalizeAlgorithmObject(
        object,
        container_set_handle.get(),
        &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to finalize algorithm inputs for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "build_mount.finalize.end algorithm=" + algorithm_name);
  }

  if (cache_shared_standard_container && !shared_key.empty()) {
    (*standard_shared_container_sets)[shared_key] = container_set_handle;
  }

  object.shared_container_set = container_set_handle;
  result.object = std::move(object);
  result.container_set = std::move(container_set_handle);
  result.ok = true;
  return result;
}

}  // namespace

bool CreateAlgorithmObjectByName(
  const std::string& algorithm_name,
  AlgorithmObject* out_group,
  std::string* out_error_message) {
  const bool emit_runner_probe =
    algorithm_name.find("v2a0_pipeline_square_vertex_demo") != std::string::npos;
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "create_object_by_name.resolve_location.begin algorithm=" + algorithm_name);
  }
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
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
  if (emit_runner_probe) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "create_object_by_name.resolve_location.end algorithm=" + algorithm_name +
        " runtime_root=" + package_location.runtime_package_root.string());
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "create_object_by_name.create_from_location.begin algorithm=" + algorithm_name);
  }

  const bool created =
    algorithm_management::CreateAlgorithmObjectFromLocation(package_location, out_group, out_error_message);
  if (emit_runner_probe && created) {
    _AppendPipelineRunnerProbe(
      "agent_mount_probe.log",
      "create_object_by_name.create_from_location.end algorithm=" + algorithm_name);
  }
  return created;
}

bool Agent::Init(AgentInitConfig config) {
  agent_name_ = std::move(config.agent_name);
  algorithm_objects_ = std::move(config.algorithm_objects);
  algorithm_runtime_states_.clear();
  algorithm_assembly_states_.clear();
  standard_shared_container_sets_.clear();
  timing_log_requested_ = false;
  algorithm_runtime_states_.reserve(algorithm_objects_.size());
  algorithm_assembly_states_.reserve(algorithm_objects_.size());
  for (const AlgorithmObject& object : algorithm_objects_) {
    AgentAlgorithmRuntimeState runtime_state{};
    runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    algorithm_runtime_states_.push_back(std::move(runtime_state));
    algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);
  }
  initialized_ = true;
  return true;
}

bool Agent::MountAlgorithm(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_index,
  std::string* out_error_message,
  AlgorithmMountMode mount_mode,
  AlgorithmExecutionPreference execution_preference) {
  auto set_error = [&](const std::string& message) {
    if (out_error_message) {
      *out_error_message = message;
    }
  };

  if (!initialized_) {
    set_error("Agent is not initialized.");
    return false;
  }

  BuiltAlgorithmMount built_mount = _BuildAlgorithmMount(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    mount_mode,
    execution_preference,
    &standard_shared_container_sets_);
  if (!built_mount.ok) {
    set_error(built_mount.error_message);
    return false;
  }

  size_t algorithm_index = 0u;
  if (!AppendAlgorithmObject(std::move(built_mount.object), &algorithm_index)) {
    set_error("Failed to append algorithm object to agent.");
    return false;
  }
  if (!BeginAlgorithmAssembly(algorithm_index)) {
    DEBUG_TOOL_ASSERT(false, "Failed to begin algorithm assembly.");
    MarkAlgorithmAssemblyFailed(algorithm_index);
    set_error("Failed to begin algorithm assembly.");
    return false;
  }

  AlgorithmObject* mounted_algorithm_object = algorithm_object(algorithm_index);
  if (!mounted_algorithm_object) {
    DEBUG_TOOL_ASSERT(false, "Failed to access algorithm object.");
    MarkAlgorithmAssemblyFailed(algorithm_index);
    set_error("Failed to access algorithm object.");
    return false;
  }
  mounted_algorithm_object->SetContainerSet(std::move(built_mount.container_set));
  MarkAlgorithmAssemblyReady(algorithm_index);

  AgentAlgorithmRuntimeState* runtime_state = algorithm_runtime_state(algorithm_index);
  if (runtime_state) {
    AlgorithmReflectionSnapshot reflection_snapshot{};
    if (CollectAlgorithmReflection(algorithm_index, &reflection_snapshot)) {
      runtime_state->reflection_snapshot = std::move(reflection_snapshot);
    }
    if (mounted_algorithm_object->algorithm_reflector) {
      DEBUG_TOOL_ASSERT(
        runtime_state->reflection_snapshot.valid,
        "Mounted algorithm reflector did not produce a valid reflection snapshot.");
    }
  }

  if (out_index) {
    *out_index = algorithm_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool Agent::MountPipelineAlgorithm(
  const std::string& pipeline_name,
  const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
  size_t* out_index,
  std::string* out_error_message,
  AlgorithmExecutionPreference execution_preference,
  AlgorithmPipelineTopology topology,
  AlgorithmPipelineSyncMode sync_mode) {
  if (!initialized_) {
    if (out_error_message) {
      *out_error_message = "Agent is not initialized.";
    }
    return false;
  }

  return algorithm_management::AlgorithmScheduler::Instance().MountPipelineAlgorithmObjects(
    &algorithm_objects_,
    &algorithm_runtime_states_,
    &algorithm_assembly_states_,
    &standard_shared_container_sets_,
    agent_name_,
    pipeline_name,
    stage_submissions,
    execution_preference,
    topology,
    sync_mode,
    out_index,
    out_error_message);
}

bool Agent::PipelineNameInUse(const std::string& pipeline_name) const {
  if (pipeline_name.empty()) {
    return false;
  }
  for (const AlgorithmObject& object : algorithm_objects_) {
    if (object.pipeline_stage && object.pipeline_name == pipeline_name) {
      return true;
    }
  }
  return false;
}

bool Agent::AppendAlgorithmObject(AlgorithmObject object, size_t* out_index) {
  if (!initialized_) {
    return false;
  }

  algorithm_objects_.push_back(std::move(object));
  algorithm_runtime_states_.push_back(AgentAlgorithmRuntimeState{});
  algorithm_runtime_states_.back().algorithm_name = algorithm_objects_.back().algorithm_profile.algorithm_name;
  algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);

  if (out_index) {
    *out_index = algorithm_objects_.size() - 1u;
  }
  return true;
}

bool Agent::RemoveAlgorithm(size_t index) {
  if (!initialized_ || index >= algorithm_objects_.size() || index >= algorithm_runtime_states_.size() ||
      index >= algorithm_assembly_states_.size()) {
    return false;
  }

  size_t erase_begin = index;
  size_t erase_end = index + 1u;
  std::string pipeline_name_to_remove{};
  if (_IsPipelineStage(algorithm_objects_[index])) {
    pipeline_name_to_remove = algorithm_objects_[index].pipeline_name;
    if (!_FindPipelineGroupRange(algorithm_objects_, index, &erase_begin, &erase_end)) {
      return false;
    }
  }

  algorithm_objects_.erase(
    algorithm_objects_.begin() + static_cast<std::ptrdiff_t>(erase_begin),
    algorithm_objects_.begin() + static_cast<std::ptrdiff_t>(erase_end));
  algorithm_runtime_states_.erase(
    algorithm_runtime_states_.begin() + static_cast<std::ptrdiff_t>(erase_begin),
    algorithm_runtime_states_.begin() + static_cast<std::ptrdiff_t>(erase_end));
  algorithm_assembly_states_.erase(
    algorithm_assembly_states_.begin() + static_cast<std::ptrdiff_t>(erase_begin),
    algorithm_assembly_states_.begin() + static_cast<std::ptrdiff_t>(erase_end));
  if (!pipeline_name_to_remove.empty()) {
        algorithm_management::AlgorithmScheduler::Instance().UnregisterPipeline(
          pipeline_name_to_remove,
          agent_name_);
  }
  return true;
}

void Agent::RefreshInterventionSignals(const AgentTickContext& context) {
  for (size_t i = 0; i < algorithm_objects_.size(); ++i) {
    if (i >= algorithm_assembly_states_.size() || algorithm_assembly_states_[i] != AlgorithmAssemblyState::Ready) {
      if (i < algorithm_runtime_states_.size()) {
        algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      }
      continue;
    }
    if (i < algorithm_runtime_states_.size() &&
        algorithm_objects_[i].tick_lifetime == AlgorithmTickLifetime::LaunchOnceThenHold &&
        algorithm_runtime_states_[i].launch_once_completed) {
      algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      continue;
    }
    AgentAlgorithmRuntimeState& runtime_state = algorithm_runtime_states_[i];
    runtime_state.agent_to_algorithm_signal = {};
    if (algorithm_objects_[i].intervention) {
      algorithm_objects_[i].intervention->FillAgentToAlgorithmSignal(
        context,
        &runtime_state.agent_to_algorithm_signal);
    }
  }
}

bool Agent::SubmitAlgorithm(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  if (!out_result) {
    return false;
  }
  if (!initialized_) {
    return false;
  }

  out_result->algorithm_to_agent_signal = {};
  out_result->algorithm_runtime_states.clear();
  out_result->algorithm_runtime_states.reserve(algorithm_objects_.size());
  out_result->timing_log.clear();

  const bool collect_timing_log = timing_log_requested_;
  timing_log_requested_ = false;
  std::ostringstream timing_log_stream{};
  if (collect_timing_log) {
    timing_log_stream << "Agent timing log for '" << agent_name_ << "'\n";
  }

  std::vector<AgentAlgorithmRuntimeState> updated_runtime_states = algorithm_runtime_states_;
  updated_runtime_states.resize(algorithm_objects_.size());
  for (size_t index = algorithm_runtime_states_.size(); index < updated_runtime_states.size(); ++index) {
    updated_runtime_states[index].algorithm_name = algorithm_objects_[index].algorithm_profile.algorithm_name;
  }
  bool pipeline_processing_failed = false;

  auto process_algorithm_index = [&](size_t index) {
    AlgorithmObject& object = algorithm_objects_[index];
    AgentAlgorithmRuntimeState runtime_state{};
    if (index < algorithm_runtime_states_.size()) {
      runtime_state = algorithm_runtime_states_[index];
    } else {
      runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    }
    _ResetRuntimeStateBase(runtime_state, &runtime_state);

    const bool allow_tick = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
    const bool is_ready =
      index < algorithm_assembly_states_.size() && algorithm_assembly_states_[index] == AlgorithmAssemblyState::Ready;
    const bool execute_now = allow_tick && is_ready;
    const auto timing_begin = std::chrono::steady_clock::now();
    if (!_TickAlgorithmObject(object, context, allow_tick, is_ready, execute_now, &runtime_state)) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
    }
    if (collect_timing_log) {
      const float elapsed_seconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - timing_begin).count();
      _AppendAlgorithmTimingLogLine(
        &timing_log_stream,
        object,
        execute_now,
        allow_tick,
        is_ready,
        elapsed_seconds);
    }

    _MergeAlgorithmToAgentSignal(runtime_state.algorithm_to_agent_signal, &out_result->algorithm_to_agent_signal);
    updated_runtime_states[index] = std::move(runtime_state);
  };

  auto process_pipeline_stage_group = [&](size_t begin_index, size_t end_index) {
    AlgorithmToAgentSignal pipeline_signal{};
    std::string pipeline_error_message;
    bool mounted_pipeline_processing_failed = false;
    if (!algorithm_management::AlgorithmScheduler::Instance().TickMountedPipeline(
          &algorithm_objects_,
          begin_index,
          end_index,
          agent_name_,
          context,
          allow_tick_mask,
          algorithm_assembly_states_,
          collect_timing_log,
          &updated_runtime_states,
          &pipeline_signal,
          &mounted_pipeline_processing_failed,
          &pipeline_error_message)) {
      const std::string failure_message = pipeline_error_message.empty()
        ? std::string("Mounted pipeline tick failed.")
        : std::move(pipeline_error_message);
      DEBUG_TOOL_ASSERT(false, failure_message.c_str());
      out_result->algorithm_to_agent_signal.stop_requested = true;
      pipeline_processing_failed = true;
      return;
    }
    if (collect_timing_log && begin_index < updated_runtime_states.size()) {
      const std::string pipeline_name = algorithm_objects_[begin_index].pipeline_name.empty()
        ? algorithm_objects_[begin_index].algorithm_profile.algorithm_name
        : algorithm_objects_[begin_index].pipeline_name;
      _AppendPipelineTimingLog(
        &timing_log_stream,
        pipeline_name,
        updated_runtime_states[begin_index]);
    }
    _MergeAlgorithmToAgentSignal(pipeline_signal, &out_result->algorithm_to_agent_signal);
    if (mounted_pipeline_processing_failed) {
      pipeline_processing_failed = true;
    }
  };

  size_t index = 0u;
  while (index < algorithm_objects_.size()) {
    const AlgorithmObject& object = algorithm_objects_[index];
    if (!object.pipeline_stage) {
      process_algorithm_index(index);
      ++index;
      continue;
    }

    size_t pipeline_begin_index = 0u;
    size_t pipeline_end_index = 0u;
    if (!_FindPipelineGroupRange(algorithm_objects_, index, &pipeline_begin_index, &pipeline_end_index)) {
      DEBUG_TOOL_ASSERT(false, "Failed to resolve pipeline stage range.");
      out_result->algorithm_to_agent_signal.stop_requested = true;
      break;
    }
    if (pipeline_begin_index != index) {
      ++index;
      continue;
    }

    process_pipeline_stage_group(pipeline_begin_index, pipeline_end_index);
    if (pipeline_processing_failed) {
      break;
    }
    index = pipeline_end_index;
  }

  for (const AgentAlgorithmRuntimeState& runtime_state : updated_runtime_states) {
    out_result->algorithm_runtime_states.push_back(runtime_state);
  }

  algorithm_runtime_states_ = std::move(updated_runtime_states);
  if (collect_timing_log) {
    out_result->timing_log = timing_log_stream.str();
  }
  return true;
}

bool Agent::Tick(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  return SubmitAlgorithm(context, allow_tick_mask, out_result);
}

bool Agent::EnqueuePipelineStage0Submission(
  const std::string& pipeline_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message) {
  if (!initialized_) {
    if (out_error_message) {
      *out_error_message = "Agent is not initialized.";
    }
    return false;
  }
  return algorithm_management::AlgorithmScheduler::Instance().EnqueueMountedPipelineStage0Submission(
    &algorithm_objects_,
    pipeline_name,
    agent_name_,
    resource_bindings,
    descriptor_values,
    &algorithm_assembly_states_,
    out_error_message);
}

bool Agent::ReplayPipelineStageBridgeDebug(
  size_t index,
  const AgentTickContext& context,
  std::string* out_error_message) {
  return algorithm_management::AlgorithmScheduler::Instance().ReplayMountedPipelineStageBridgeDebug(
    &algorithm_objects_,
    index,
    context,
    &algorithm_runtime_states_,
    out_error_message);
}

void Agent::RequestTimingLog() {
  timing_log_requested_ = true;
}

bool Agent::BeginAlgorithmAssembly(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return false;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Assembling;
  return true;
}

void Agent::MarkAlgorithmAssemblyReady(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Ready;
}

void Agent::MarkAlgorithmAssemblyFailed(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Failed;
}

bool Agent::GetAlgorithmAssemblySlot(size_t index, AlgorithmAssemblySlot* out_slot) {
  if (!out_slot || index >= algorithm_objects_.size() || index >= algorithm_assembly_states_.size()) {
    return false;
  }

  out_slot->index = index;
  out_slot->algorithm_object = &algorithm_objects_[index];
  out_slot->assembly_state = &algorithm_assembly_states_[index];
  return true;
}

bool Agent::CollectAlgorithmReflection(size_t index, AlgorithmReflectionSnapshot* out_snapshot) const {
  if (!out_snapshot || index >= algorithm_objects_.size()) {
    return false;
  }
  const AlgorithmObject& object = algorithm_objects_[index];
  const AlgorithmContainerSet* container_set = algorithm_objects_[index].container_set();
  if (!container_set) {
    return false;
  }
  return _CollectReflectionSnapshot(object, *container_set, out_snapshot);
}

void Agent::Destroy() {
  for (const AlgorithmObject& object : algorithm_objects_) {
    if (object.pipeline_stage &&
        object.pipeline_stage_index == 0u &&
        !object.pipeline_name.empty()) {
      algorithm_management::AlgorithmScheduler::Instance().UnregisterPipeline(
        object.pipeline_name,
        agent_name_);
    }
  }
  initialized_ = false;
  agent_name_.clear();
  algorithm_objects_.clear();
  algorithm_runtime_states_.clear();
  algorithm_assembly_states_.clear();
  standard_shared_container_sets_.clear();
  timing_log_requested_ = false;
}

}  // namespace agent
