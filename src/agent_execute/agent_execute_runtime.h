#pragma once

#include "agent/agent.h"
#include "agent_execute/agent_ticker.h"
#include "common_data/input_state.h"
#include "common_data/mesh.h"

#include <memory>

namespace agent_execute {

struct AgentLaunchSpec {
  agent::AgentInitConfig agent_config;
};

class AgentExecuteRuntime {
 public:
  bool LoadAgent(AgentLaunchSpec spec);
  bool UnloadAgent();
  bool Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  const std::shared_ptr<agent::Agent>& loaded_agent() const { return loaded_agent_; }
  bool has_agent() const { return static_cast<bool>(loaded_agent_); }
  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return agent_ticker_.algorithm_to_agent_signal(); }
  const AgentToAlgorithmSignal& agent_to_algorithm_signal() const { return agent_ticker_.agent_to_algorithm_signal(); }

 private:
  std::shared_ptr<agent::Agent> loaded_agent_{};
  AgentTicker agent_ticker_{};
};

}  // namespace agent_execute

using agent_execute::AgentExecuteRuntime;
using agent_execute::AgentLaunchSpec;
