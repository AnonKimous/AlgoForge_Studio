#include "agent_ticker.h"

#include "capabilities/agent/agent.h"

#include <utility>

namespace agent_management {

namespace {

bool _SignalBlocksTick(
  const agent::AgentAlgorithmCodecGroup& group,
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

void AgentTicker::Init(std::shared_ptr<agent::Agent> agent) {
  agent_binding_ = std::move(agent);
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
}

void AgentTicker::ApplyInterventionRequest(const InteractionInterventionRequest& request) {
  intervention_request_ = request;
}

void AgentTicker::Tick(
  Mesh& mesh,
  const InputState& input,
  Vec2 mouse_pixel,
  float dt_seconds) {
  algorithm_to_agent_signal_ = {};
  if (!agent_binding_) {
    return;
  }

  const agent::AgentTickContext context{
    .mesh = &mesh,
    .input = &input,
    .mouse_pixel = mouse_pixel,
    .dt_seconds = dt_seconds,
    .intervention_request = &intervention_request_,
  };
  agent_binding_->RefreshInterventionSignals(context);

  std::vector<bool> allow_tick_mask(agent_binding_->algorithm_count(), true);
  for (size_t i = 0; i < agent_binding_->algorithm_count(); ++i) {
    const agent::AgentAlgorithmCodecGroup* group = agent_binding_->algorithm_codec_group(i);
    const agent::AgentAlgorithmRuntimeState* runtime_state = agent_binding_->algorithm_runtime_state(i);
    if (!group || !runtime_state) {
      allow_tick_mask[i] = false;
      continue;
    }
    if (_SignalBlocksTick(*group, runtime_state->agent_to_algorithm_signal)) {
      allow_tick_mask[i] = false;
    }
  }

  agent::AgentTickResult result{};
  if (agent_binding_->Tick(context, allow_tick_mask, &result)) {
    algorithm_to_agent_signal_ = result.algorithm_to_agent_signal;
  }
}

void AgentTicker::Destroy() {
  agent_binding_.reset();
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
}

}  // namespace agent_management

