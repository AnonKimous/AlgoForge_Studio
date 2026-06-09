#include "agent.h"

#include <utility>

namespace agent {

namespace {

void _CollectDebugState(
  const AgentAlgorithmCodecGroup& group,
  AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }

  if (auto* complex_codec = dynamic_cast<IComplexAlgorithmPackageCodec*>(group.reflector.get())) {
    if (out_debug_state) {
      complex_codec->CollectDebugState(out_debug_state);
    }
  }
}

bool _CollectReflectionSnapshot(
  const AgentAlgorithmCodecGroup& group,
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
  const AgentAlgorithmCodecGroup& group,
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

bool Agent::Init(AgentInitConfig config) {
  agent_name_ = std::move(config.agent_name);
  algorithm_codec_groups_ = std::move(config.algorithm_codec_groups);
  algorithm_runtime_states_.clear();
  algorithm_objects_.clear();
  algorithm_assembly_states_.clear();
  algorithm_runtime_states_.reserve(algorithm_codec_groups_.size());
  algorithm_objects_.reserve(algorithm_codec_groups_.size());
  algorithm_assembly_states_.reserve(algorithm_codec_groups_.size());
  for (const AgentAlgorithmCodecGroup& group : algorithm_codec_groups_) {
    AgentAlgorithmRuntimeState runtime_state{};
    runtime_state.algorithm_name = group.algorithm_profile.algorithm_name;
    algorithm_runtime_states_.push_back(std::move(runtime_state));
    algorithm_objects_.push_back({});
    algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);
  }
  initialized_ = true;
  return true;
}

bool Agent::AppendAlgorithmCodecGroup(AgentAlgorithmCodecGroup group, size_t* out_index) {
  if (!initialized_) {
    return false;
  }

  algorithm_codec_groups_.push_back(std::move(group));
  algorithm_runtime_states_.push_back(AgentAlgorithmRuntimeState{});
  algorithm_runtime_states_.back().algorithm_name = algorithm_codec_groups_.back().algorithm_profile.algorithm_name;
  algorithm_objects_.push_back({});
  algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);

  if (out_index) {
    *out_index = algorithm_codec_groups_.size() - 1u;
  }
  return true;
}

void Agent::RefreshInterventionSignals(const AgentTickContext& context) {
  for (size_t i = 0; i < algorithm_codec_groups_.size(); ++i) {
    if (i >= algorithm_assembly_states_.size() || algorithm_assembly_states_[i] != AlgorithmAssemblyState::Ready) {
      if (i < algorithm_runtime_states_.size()) {
        algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      }
      continue;
    }
    AgentAlgorithmRuntimeState& runtime_state = algorithm_runtime_states_[i];
    runtime_state.agent_to_algorithm_signal = {};
    if (algorithm_codec_groups_[i].intervention) {
      algorithm_codec_groups_[i].intervention->FillAgentToAlgorithmSignal(
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
  out_result->algorithm_runtime_states.reserve(algorithm_codec_groups_.size());

  std::vector<AgentAlgorithmRuntimeState> updated_runtime_states;
  updated_runtime_states.reserve(algorithm_codec_groups_.size());

  for (size_t i = 0; i < algorithm_codec_groups_.size(); ++i) {
    const AgentAlgorithmCodecGroup& group = algorithm_codec_groups_[i];
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
    if (allow_tick && is_ready) {
      if (group.temporaryTest_main_thread_executor && i < algorithm_objects_.size()) {
        if (!group.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
              context,
              group.algorithm_profile,
              runtime_state.agent_to_algorithm_signal,
              algorithm_objects_[i].mutable_container_set(),
              &runtime_state.algorithm_to_agent_signal,
              &runtime_state.debug_state)) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
      }
      AlgorithmPackageDebugState collected_debug_state{};
      _CollectDebugState(group, &collected_debug_state);
      runtime_state.debug_state.signals.insert(
        runtime_state.debug_state.signals.end(),
        collected_debug_state.signals.begin(),
        collected_debug_state.signals.end());
      if (runtime_state.agent_to_algorithm_signal.reflection_collection_requested) {
        runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
        if (_CollectReflectionSnapshot(group, *algorithm_objects_[i].container_set(), &runtime_state.reflection_snapshot)) {
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

    out_result->algorithm_runtime_states.push_back(runtime_state);
    updated_runtime_states.push_back(std::move(runtime_state));
  }

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
  if (!out_slot || index >= algorithm_codec_groups_.size() || index >= algorithm_objects_.size() ||
      index >= algorithm_assembly_states_.size()) {
    return false;
  }

  out_slot->index = index;
  out_slot->algorithm_codec_group = &algorithm_codec_groups_[index];
  out_slot->algorithm_object = &algorithm_objects_[index];
  out_slot->assembly_state = &algorithm_assembly_states_[index];
  return true;
}

bool Agent::CollectAlgorithmReflection(size_t index, AlgorithmReflectionSnapshot* out_snapshot) const {
  if (!out_snapshot || index >= algorithm_codec_groups_.size() || index >= algorithm_objects_.size()) {
    return false;
  }
  const AgentAlgorithmCodecGroup& group = algorithm_codec_groups_[index];
  const AlgorithmContainerSet& container_set = *algorithm_objects_[index].container_set();
  return _CollectReflectionSnapshot(group, container_set, out_snapshot);
}

void Agent::Destroy() {
  initialized_ = false;
  agent_name_.clear();
  algorithm_codec_groups_.clear();
  algorithm_runtime_states_.clear();
  algorithm_objects_.clear();
  algorithm_assembly_states_.clear();
}

}  // namespace agent
