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

bool _SignalBlocksTick(
  const AgentAlgorithmCodecGroup& group,
  const AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !group.intervention_algorithm || group.intervention_algorithm->SupportsIntervention();
}

}  // namespace

bool Agent::Init(AgentInitConfig config) {
  agent_name_ = std::move(config.agent_name);
  algorithm_codec_groups_ = std::move(config.algorithm_codec_groups);
  algorithm_runtime_states_.clear();
  algorithm_container_sets_.clear();
  algorithm_runtime_states_.reserve(algorithm_codec_groups_.size());
  algorithm_container_sets_.reserve(algorithm_codec_groups_.size());
  for (const AgentAlgorithmCodecGroup& group : algorithm_codec_groups_) {
    const std::string manifest_name = ResolveAlgorithmManifestName(group.algorithm_profile);
    if (manifest_name.empty()) {
      initialized_ = false;
      algorithm_runtime_states_.clear();
      algorithm_container_sets_.clear();
      return false;
    }

    AlgorithmContainerSet container_set{};
    if (!AlgorithmManager_CreateContainersFromManifestName(manifest_name, &container_set)) {
      initialized_ = false;
      algorithm_runtime_states_.clear();
      algorithm_container_sets_.clear();
      return false;
    }
    if (group.decomposer) {
      std::string decompose_error_message;
      if (!group.decomposer->Decompose(
            group.algorithm_profile,
            group.resource_bindings,
            group.descriptor_values,
            &container_set,
            &decompose_error_message)) {
        initialized_ = false;
        algorithm_runtime_states_.clear();
        algorithm_container_sets_.clear();
        return false;
      }
    }

    AgentAlgorithmRuntimeState runtime_state{};
    runtime_state.algorithm_name = group.algorithm_profile.algorithm_name;
    algorithm_runtime_states_.push_back(std::move(runtime_state));
    algorithm_container_sets_.push_back(std::move(container_set));
  }
  initialized_ = true;
  return true;
}

void Agent::RefreshInterventionSignals(const AgentTickContext& context) {
  for (size_t i = 0; i < algorithm_codec_groups_.size(); ++i) {
    AgentAlgorithmRuntimeState& runtime_state = algorithm_runtime_states_[i];
    runtime_state.agent_to_algorithm_signal = {};
    if (algorithm_codec_groups_[i].intervention_agent) {
      algorithm_codec_groups_[i].intervention_agent->FillAgentToAlgorithmSignal(
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

    const bool allow_tick = i < allow_tick_mask.size() ? allow_tick_mask[i] : true;
    if (allow_tick) {
      if (group.temporaryTest_main_thread_executor && i < algorithm_container_sets_.size()) {
        if (!group.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
              context,
              group.algorithm_profile,
              runtime_state.agent_to_algorithm_signal,
              &algorithm_container_sets_[i],
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
    } else {
      runtime_state.algorithm_to_agent_signal.pause_requested =
        runtime_state.agent_to_algorithm_signal.pause_requested;
      runtime_state.algorithm_to_agent_signal.stop_requested =
        runtime_state.agent_to_algorithm_signal.stop_requested;
      runtime_state.algorithm_to_agent_signal.intervention_needed =
        _SignalBlocksTick(group, runtime_state.agent_to_algorithm_signal) &&
        runtime_state.agent_to_algorithm_signal.needs_intervention;
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

    out_result->algorithm_runtime_states.push_back(runtime_state);
    updated_runtime_states.push_back(std::move(runtime_state));
  }

  algorithm_runtime_states_ = std::move(updated_runtime_states);
  return true;
}

void Agent::Destroy() {
  initialized_ = false;
  agent_name_.clear();
  algorithm_codec_groups_.clear();
  algorithm_runtime_states_.clear();
  algorithm_container_sets_.clear();
}

}  // namespace agent
