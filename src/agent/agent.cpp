#include "agent/agent.h"

#include "algorithm_management/algorithm_manager.h"
#include "algorithm_support/algorithm_protocol.h"

#include <algorithm>
#include <utility>

namespace agent {

namespace {

void _CollectDebugState(
  const AgentAlgorithmSupportGroup& group,
  AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }

  if (auto* complex_support = dynamic_cast<IComplexAlgorithmPackageSupport*>(group.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

bool _CollectReflectionSnapshot(
  const AgentAlgorithmSupportGroup& group,
  const AlgorithmContainerSet& container_set,
  AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }

  out_snapshot->Clear();
  out_snapshot->algorithm_name = group.algorithm_profile.algorithm_name;
  if (!group.algorithm_reflector) {
    return false;
  }

  for (const auto& [reflection_object_name, binding] : group.algorithm_reflector->container_bindings_by_reflection_object_name) {
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

bool _SignalBlocksTick(
  const AgentAlgorithmSupportGroup& group,
  const AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !group.intervention || group.intervention->SupportsIntervention();
}

}  // namespace

bool CreateAlgorithmSupportGroupByName(
  const std::string& algorithm_name,
  AgentAlgorithmSupportGroup* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AgentAlgorithmSupportGroup output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
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

  return algorithm_support::CreateAlgorithmSupportGroupFromLocation(package_location, out_group, out_error_message);
}

bool Agent::Init(AgentInitConfig config) {
  agent_name_ = std::move(config.agent_name);
  algorithm_support_groups_ = std::move(config.algorithm_support_groups);
  algorithm_runtime_states_.clear();
  algorithm_objects_.clear();
  algorithm_assembly_states_.clear();
  algorithm_runtime_states_.reserve(algorithm_support_groups_.size());
  algorithm_objects_.reserve(algorithm_support_groups_.size());
  algorithm_assembly_states_.reserve(algorithm_support_groups_.size());
  for (const AgentAlgorithmSupportGroup& group : algorithm_support_groups_) {
    AgentAlgorithmRuntimeState runtime_state{};
    runtime_state.algorithm_name = group.algorithm_profile.algorithm_name;
    algorithm_runtime_states_.push_back(std::move(runtime_state));
    algorithm_objects_.emplace_back();
    algorithm_objects_.back().SetContainerSet(group.shared_container_set);
    algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);
  }
  initialized_ = true;
  return true;
}

bool Agent::AppendAlgorithmSupportGroup(AgentAlgorithmSupportGroup group, size_t* out_index) {
  if (!initialized_) {
    return false;
  }

  algorithm_support_groups_.push_back(std::move(group));
  algorithm_runtime_states_.push_back(AgentAlgorithmRuntimeState{});
  algorithm_runtime_states_.back().algorithm_name = algorithm_support_groups_.back().algorithm_profile.algorithm_name;
  algorithm_objects_.emplace_back();
  algorithm_objects_.back().SetContainerSet(algorithm_support_groups_.back().shared_container_set);
  algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);

  if (out_index) {
    *out_index = algorithm_support_groups_.size() - 1u;
  }
  return true;
}

bool Agent::RemoveAlgorithm(size_t index) {
  if (!initialized_ || index >= algorithm_support_groups_.size() || index >= algorithm_runtime_states_.size() ||
      index >= algorithm_objects_.size() || index >= algorithm_assembly_states_.size()) {
    return false;
  }

  algorithm_support_groups_.erase(algorithm_support_groups_.begin() + static_cast<std::ptrdiff_t>(index));
  algorithm_runtime_states_.erase(algorithm_runtime_states_.begin() + static_cast<std::ptrdiff_t>(index));
  algorithm_objects_.erase(algorithm_objects_.begin() + static_cast<std::ptrdiff_t>(index));
  algorithm_assembly_states_.erase(algorithm_assembly_states_.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

void Agent::RefreshInterventionSignals(const AgentTickContext& context) {
  for (size_t i = 0; i < algorithm_support_groups_.size(); ++i) {
    if (i >= algorithm_assembly_states_.size() || algorithm_assembly_states_[i] != AlgorithmAssemblyState::Ready) {
      if (i < algorithm_runtime_states_.size()) {
        algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      }
      continue;
    }
    AgentAlgorithmRuntimeState& runtime_state = algorithm_runtime_states_[i];
    runtime_state.agent_to_algorithm_signal = {};
    if (algorithm_support_groups_[i].intervention) {
      algorithm_support_groups_[i].intervention->FillAgentToAlgorithmSignal(
        context,
        &runtime_state.agent_to_algorithm_signal);
    }
  }
}

bool Agent::Tick(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  if (!out_result) {
    return false;
  }

  out_result->algorithm_to_agent_signal = {};
  out_result->algorithm_runtime_states.clear();
  out_result->algorithm_runtime_states.reserve(algorithm_support_groups_.size());
  algorithm_execution_end_queue_.clear();

  std::vector<AgentAlgorithmRuntimeState> updated_runtime_states(algorithm_support_groups_.size());

  auto process_index = [&](size_t i) {
    const AgentAlgorithmSupportGroup& group = algorithm_support_groups_[i];
    AgentAlgorithmRuntimeState runtime_state{};
    if (i < algorithm_runtime_states_.size()) {
      runtime_state = algorithm_runtime_states_[i];
    } else {
      runtime_state.algorithm_name = group.algorithm_profile.algorithm_name;
    }
    runtime_state.algorithm_to_agent_signal = {};
    runtime_state.debug_state = {};
    runtime_state.reflection_snapshot.Clear();

    const bool allow_tick = i < allow_tick_mask.size() ? allow_tick_mask[i] : true;
    const bool is_ready =
      i < algorithm_assembly_states_.size() && algorithm_assembly_states_[i] == AlgorithmAssemblyState::Ready;
    AlgorithmObject* algorithm_object = i < algorithm_objects_.size() ? &algorithm_objects_[i] : nullptr;
    if (allow_tick && is_ready) {
      const bool allow_gpu_execution =
        group.gpu_symbol &&
        group.execution_preference == AlgorithmExecutionPreference::Gpu;
      const bool allow_cpu_execution =
        group.cpu_symbol &&
        group.execution_preference == AlgorithmExecutionPreference::Cpu;
      bool gpu_tick_executed = false;
      if (allow_gpu_execution && algorithm_object) {
        std::string gpu_error_message;
        gpu_tick_executed = algorithm_management::job_gpu::Execute(
          group,
          algorithm_objects_[i].mutable_container_set(),
          context,
          &gpu_error_message);
        if (!gpu_tick_executed && !gpu_error_message.empty()) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
        if (gpu_tick_executed) {
          algorithm_execution_end_queue_.push_back(i);
        }
      }
      if (allow_cpu_execution && !gpu_tick_executed && group.temporaryTest_main_thread_executor && algorithm_object) {
        if (!algorithm_management::job_cpu::Execute(
              group,
              context,
              runtime_state.agent_to_algorithm_signal,
              algorithm_objects_[i].mutable_container_set(),
              &runtime_state.algorithm_to_agent_signal,
              &runtime_state.debug_state)) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
      }
      const bool needs_gpu_state_sync =
        gpu_tick_executed &&
        algorithm_object &&
        algorithm_object->container_set() &&
        (
          (group.algorithm_reflector && !group.algorithm_reflector->empty()) ||
          runtime_state.agent_to_algorithm_signal.reflection_collection_requested);
      if (needs_gpu_state_sync) {
        std::string gpu_sync_error_message;
        if (!algorithm_management::job_gpu::Synchronize(
              group,
              algorithm_object->mutable_container_set(),
              &gpu_sync_error_message)) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
      }
      AlgorithmPackageDebugState collected_debug_state{};
      _CollectDebugState(group, &collected_debug_state);
      runtime_state.debug_state.signals.insert(
        runtime_state.debug_state.signals.end(),
        collected_debug_state.signals.begin(),
        collected_debug_state.signals.end());
      const bool has_runtime_reflector = group.algorithm_reflector && !group.algorithm_reflector->empty();
      if (has_runtime_reflector && algorithm_object && algorithm_object->container_set() &&
          _CollectReflectionSnapshot(group, *algorithm_object->container_set(), &runtime_state.reflection_snapshot)) {
        runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
      } else if (runtime_state.agent_to_algorithm_signal.reflection_collection_requested) {
        runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
        if (algorithm_object && algorithm_object->container_set() &&
            _CollectReflectionSnapshot(group, *algorithm_object->container_set(), &runtime_state.reflection_snapshot)) {
          runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
        }
      }
    } else if (is_ready) {
      runtime_state.algorithm_to_agent_signal.pause_requested =
        runtime_state.agent_to_algorithm_signal.pause_requested;
      runtime_state.algorithm_to_agent_signal.stop_requested =
        runtime_state.agent_to_algorithm_signal.stop_requested;
      runtime_state.algorithm_to_agent_signal.intervention_needed =
        _SignalBlocksTick(group, runtime_state.agent_to_algorithm_signal) &&
        runtime_state.agent_to_algorithm_signal.needs_intervention;
      runtime_state.algorithm_to_agent_signal.reflection_collection_requested =
        runtime_state.agent_to_algorithm_signal.reflection_collection_requested;
    } else {
      runtime_state.agent_to_algorithm_signal = {};
      runtime_state.algorithm_to_agent_signal = {};
    }

    out_result->algorithm_to_agent_signal.intervention_applied =
      out_result->algorithm_to_agent_signal.intervention_applied ||
      runtime_state.algorithm_to_agent_signal.intervention_applied;
    out_result->algorithm_to_agent_signal.pause_requested =
      out_result->algorithm_to_agent_signal.pause_requested ||
      runtime_state.algorithm_to_agent_signal.pause_requested;
    out_result->algorithm_to_agent_signal.stop_requested =
      out_result->algorithm_to_agent_signal.stop_requested ||
      runtime_state.algorithm_to_agent_signal.stop_requested;
    out_result->algorithm_to_agent_signal.intervention_needed =
      out_result->algorithm_to_agent_signal.intervention_needed ||
      runtime_state.algorithm_to_agent_signal.intervention_needed;
    out_result->algorithm_to_agent_signal.reflection_collection_requested =
      out_result->algorithm_to_agent_signal.reflection_collection_requested ||
      runtime_state.algorithm_to_agent_signal.reflection_collection_requested;

    updated_runtime_states[i] = std::move(runtime_state);
  };

  for (size_t index = 0; index < algorithm_support_groups_.size(); ++index) {
    process_index(index);
  }

  for (const AgentAlgorithmRuntimeState& runtime_state : updated_runtime_states) {
    out_result->algorithm_runtime_states.push_back(runtime_state);
  }

  algorithm_execution_end_queue_.clear();
  algorithm_runtime_states_ = std::move(updated_runtime_states);
  return true;
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
  if (!out_slot || index >= algorithm_support_groups_.size() || index >= algorithm_objects_.size() ||
      index >= algorithm_assembly_states_.size()) {
    return false;
  }

  out_slot->index = index;
  out_slot->algorithm_support_group = &algorithm_support_groups_[index];
  out_slot->algorithm_object = &algorithm_objects_[index];
  out_slot->assembly_state = &algorithm_assembly_states_[index];
  return true;
}

bool Agent::CollectAlgorithmReflection(size_t index, AlgorithmReflectionSnapshot* out_snapshot) const {
  if (!out_snapshot || index >= algorithm_support_groups_.size() || index >= algorithm_objects_.size()) {
    return false;
  }
  const AgentAlgorithmSupportGroup& group = algorithm_support_groups_[index];
  const AlgorithmContainerSet* container_set = algorithm_objects_[index].container_set();
  if (!container_set) {
    return false;
  }
  return _CollectReflectionSnapshot(group, *container_set, out_snapshot);
}

void Agent::Destroy() {
  initialized_ = false;
  agent_name_.clear();
  algorithm_support_groups_.clear();
  algorithm_runtime_states_.clear();
  algorithm_objects_.clear();
  algorithm_assembly_states_.clear();
  algorithm_execution_end_queue_.clear();
}

}  // namespace agent
