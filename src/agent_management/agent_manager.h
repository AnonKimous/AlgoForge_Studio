#pragma once

#include "capabilities/agent/agent.h"
#include "common_data/common_data.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace agent_management {

struct AgentCreateSpec {
  agent::AgentInitConfig agent_config;
};

class AgentManager {
 public:
  AgentManager();
  ~AgentManager();

  bool CreateAgent(AgentCreateSpec spec, size_t* out_agent_index = nullptr);
  bool DestroyAgent(size_t agent_index);
  void ClearAgents();
  bool Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  size_t agent_count() const;
  bool has_agents() const;
  std::shared_ptr<agent::Agent> agent(size_t index) const;
  const AlgorithmToAgentSignal& combined_algorithm_to_agent_signal() const {
    return combined_algorithm_to_agent_signal_;
  }

 private:
  struct ManagedAgentEntry;

  std::vector<std::shared_ptr<ManagedAgentEntry>> managed_agents_{};
  AlgorithmToAgentSignal combined_algorithm_to_agent_signal_{};
};

}  // namespace agent_management

using agent_management::AgentCreateSpec;
using agent_management::AgentManager;
