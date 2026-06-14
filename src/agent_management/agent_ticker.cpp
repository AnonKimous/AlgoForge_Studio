#include "agent_ticker.h"

#include "agent/agent.h"

#include <utility>

namespace agent_management {

namespace {

bool _SignalBlocksTick(
  const agent::AlgorithmObject& object,
  const AgentToAlgorithmSignal& signal) {
  if ((signal.control_bits & kInterventionControlStopAndEditBit) != 0u) {
    return true;
  }
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  (void)object;
  (void)signal;
  return false;
}

}  // namespace

void AgentTicker::Init(std::shared_ptr<agent::Agent> agent) {
  agent_binding_ = std::move(agent);
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
  intervention_request_.enabled = true;
}

void AgentTicker::ApplyInterventionRequest(const InteractionInterventionRequest& request) {
  intervention_request_ = request;
}

void AgentTicker::Tick(
  const InputState& input,
  Vec2 mouse_pixel,
  float dt_seconds) {
  algorithm_to_agent_signal_ = {};
  if (!agent_binding_) {
    return;
  }

  const agent::AgentTickContext context{
    .input = &input,
    .mouse_pixel = mouse_pixel,
    .dt_seconds = dt_seconds,
    .intervention_request = &intervention_request_,
  };
  agent_binding_->RefreshInterventionSignals(context);

  std::vector<bool> allow_tick_mask(agent_binding_->algorithm_count(), true);
  for (size_t i = 0; i < agent_binding_->algorithm_count(); ++i) {
    const agent::AlgorithmObject* object = agent_binding_->algorithm_object(i);
    const agent::AgentAlgorithmRuntimeState* runtime_state = agent_binding_->algorithm_runtime_state(i);
    if (!object || !runtime_state) {
      allow_tick_mask[i] = false;
      continue;
    }
    if (_SignalBlocksTick(*object, runtime_state->agent_to_algorithm_signal)) {
      allow_tick_mask[i] = false;
    }
  }

  agent::AgentTickResult result{};
  if (agent_binding_->Tick(context, allow_tick_mask, &result)) {
    algorithm_to_agent_signal_ = result.algorithm_to_agent_signal;
  } else {
    algorithm_to_agent_signal_ = {};
  }
}

void AgentTicker::Destroy() {
  agent_binding_.reset();
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
}

}  // namespace agent_management

