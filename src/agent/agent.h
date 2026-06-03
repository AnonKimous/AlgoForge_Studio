#pragma once

#include <string>
#include <utility>

namespace agent {

struct AgentInitConfig {
  std::string agent_name;
  std::string algorithm_name;
};

class Agent {
 public:
  bool Init(AgentInitConfig config) {
    initialized_ = true;
    agent_name_ = std::move(config.agent_name);
    algorithm_name_ = std::move(config.algorithm_name);
    return true;
  }

  void Destroy() {
    initialized_ = false;
    agent_name_.clear();
    algorithm_name_.clear();
  }

  bool initialized() const { return initialized_; }
  const std::string& agent_name() const { return agent_name_; }
  const std::string& algorithm_name() const { return algorithm_name_; }

 private:
  bool initialized_{false};
  std::string algorithm_name_{};
  std::string agent_name_{};
};

using AgentRuntime = Agent;

inline bool agent_init(Agent* agent_instance, AgentInitConfig config) {
  if (!agent_instance) return false;
  return agent_instance->Init(std::move(config));
}

inline void agent_destroy(Agent* agent_instance) {
  if (agent_instance) {
    agent_instance->Destroy();
  }
}

}  // namespace agent

using agent::Agent;
using agent::AgentInitConfig;
using agent::AgentRuntime;
using agent::agent_destroy;
using agent::agent_init;
