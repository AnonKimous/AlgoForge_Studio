#pragma once

#include "common_data/input_state.h"
#include "common_data/interaction/interaction_signals.h"
#include "common_data/mesh.h"

#include <memory>

namespace agent {
class Agent;
}

namespace agent_execute {

class AgentTicker {
 public:
  void Init(std::shared_ptr<agent::Agent> agent);
  void Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  void ApplyInterventionRequest(const InteractionInterventionRequest& request);
  void SetAgentToAlgorithmSignal(const AgentToAlgorithmSignal& signal) { agent_to_algorithm_signal_ = signal; }

  const AgentToAlgorithmSignal& agent_to_algorithm_signal() const { return agent_to_algorithm_signal_; }
  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return algorithm_to_agent_signal_; }
  const InteractionInterventionRequest& intervention_request() const { return intervention_request_; }

 private:
  std::shared_ptr<agent::Agent> agent_binding_{};
  AgentToAlgorithmSignal agent_to_algorithm_signal_{};
  AlgorithmToAgentSignal algorithm_to_agent_signal_{};
  InteractionInterventionRequest intervention_request_{};
};

}  // namespace agent_execute

using agent_execute::AgentTicker;
