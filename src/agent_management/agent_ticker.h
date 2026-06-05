#pragma once

#if !defined(AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD) && !defined(AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include agent_management/agent_ticker.h directly. Use agent_management/agent_manager.h."
#endif

#include "common_data/common_data.h"

#include <memory>
#include <vector>

namespace agent {
class Agent;
}

namespace agent_management {

class AgentTicker {
 public:
  void Init(std::shared_ptr<agent::Agent> agent);
  void Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  void ApplyInterventionRequest(const InteractionInterventionRequest& request);

  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return algorithm_to_agent_signal_; }
  const InteractionInterventionRequest& intervention_request() const { return intervention_request_; }

 private:
  std::shared_ptr<agent::Agent> agent_binding_{};
  AlgorithmToAgentSignal algorithm_to_agent_signal_{};
  InteractionInterventionRequest intervention_request_{};
};

}  // namespace agent_management

