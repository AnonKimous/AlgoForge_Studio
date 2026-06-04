#include "agent_ticker.h"

#include <utility>

namespace agent_execute {

void AgentTicker::Init(std::shared_ptr<agent::Agent> agent) {
  agent_binding_ = std::move(agent);
  agent_to_algorithm_signal_ = {};
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
  (void)mesh;
  (void)input;
  (void)mouse_pixel;
  (void)dt_seconds;

  algorithm_to_agent_signal_ = {};
  if (!agent_binding_) {
    agent_to_algorithm_signal_ = {};
    return;
  }
  if (agent_to_algorithm_signal_.stop_requested) {
    algorithm_to_agent_signal_.stop_requested = true;
    agent_to_algorithm_signal_ = {};
    return;
  }
  if (agent_to_algorithm_signal_.pause_requested) {
    algorithm_to_agent_signal_.pause_requested = true;
    agent_to_algorithm_signal_ = {};
    return;
  }
  if (agent_to_algorithm_signal_.needs_intervention && intervention_request_.enabled) {
    algorithm_to_agent_signal_.intervention_needed = true;
    agent_to_algorithm_signal_ = {};
    return;
  }
  agent_to_algorithm_signal_ = {};
}

void AgentTicker::Destroy() {
  agent_binding_.reset();
  agent_to_algorithm_signal_ = {};
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
}

}  // namespace agent_execute
