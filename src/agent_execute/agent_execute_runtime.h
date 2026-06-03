#pragma once

#include "agent/agent.h"
#include "agent_execute/agent_ticker.h"
#include "algorithm_library/algorithm_types.h"
#include "common_data/input_state.h"
#include "common_data/mesh.h"

#include <memory>
#include <string>
#include <utility>

namespace agent_execute {

struct AgentLaunchSpec {
  agent::AgentInitConfig agent_config;
  PhysSolverConfig solver_config{};
  AlgorithmComplianceDescriptor compliance_descriptor{};
};

AgentLaunchSpec CreateCameraAgentInfo(const Mesh& mesh);
AgentLaunchSpec CreatePhysicsAgentInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path = "");

class AgentExecuteRuntime {
 public:
  bool LoadAgent(AgentLaunchSpec spec);
  bool UnloadAgent();
  bool Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  const std::shared_ptr<agent::Agent>& current_agent() const { return current_agent_; }
  bool has_agent() const { return static_cast<bool>(current_agent_); }
  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return agent_ticker_.algorithm_to_agent_signal(); }
  const AgentToAlgorithmSignal& agent_to_algorithm_signal() const { return agent_ticker_.agent_to_algorithm_signal(); }

 private:
  std::shared_ptr<agent::Agent> current_agent_{};
  AgentLaunchSpec current_launch_spec_{};
  AgentTicker agent_ticker_{};
};

}  // namespace agent_execute

using agent_execute::AgentExecuteRuntime;
using agent_execute::AgentLaunchSpec;
using agent_execute::CreateCameraAgentInfo;
using agent_execute::CreatePhysicsAgentInfo;
