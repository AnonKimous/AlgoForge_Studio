#pragma once

#include "agent/agent.h"
#include "agents/agents.h"
#include "agent_execute/agent_ticker.h"
#include "algorithm_library/algorithm_types.h"
#include "common_data/mesh.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace agent_execute {

agent::AgentInitConfig CreateCameraAgentInfo(const Mesh& mesh);
agent::AgentInitConfig CreatePhysicsAgentInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path = "");

class AgentExecuteRuntime {
 public:
  using AgentHandle = std::size_t;
  static constexpr AgentHandle kInvalidAgentHandle = static_cast<AgentHandle>(-1);

  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool LoadMeshFromFile(const std::string& path, std::string* error_message = nullptr);
  AgentHandle LoadAgent(agent::AgentInitConfig info);
  bool UnloadAgent(AgentHandle handle);
  void SetDrawCallback(std::function<void()> draw_callback);
  bool createagent(agent::AgentInitConfig info) { return LoadAgent(std::move(info)) != kInvalidAgentHandle; }
  bool destroyagent(AgentHandle handle) { return UnloadAgent(handle); }
  bool Tick();
  void Destroy();

  const Mesh& mesh() const { return mesh_; }
  Mesh& mutable_mesh() { return mesh_; }
  const std::string& mesh_source_path() const { return mesh_source_path_; }
  const std::vector<std::shared_ptr<agent::Agent>>& agent_slots() const { return agent_slots_; }
  const std::shared_ptr<agent::Agent>& active_agent_binding() const { return active_agent_binding_; }
  bool active_agent_binding_ready() const { return active_agent_binding_ready_; }
  const std::vector<Vec3>& active_vertex_positions() const;
  const std::string& ui_status_message() const { return ui_status_message_; }
  void SetUiStatusMessage(std::string message);

 private:
  bool LoadMeshAndResetBindings(const std::string& path, std::string* status_message);
  bool MountActiveAgent(const std::shared_ptr<agent::Agent>& agent);
  void RefreshBindingsFromAgentSlots();

  Mesh mesh_{};
  std::string mesh_source_path_{};
  agent_execute::WindowAgent window_agent_{};
  agent_execute::AgentTicker agent_ticker_{};
  std::vector<std::shared_ptr<agent::Agent>> agent_slots_{};
  std::shared_ptr<agent::Agent> active_agent_binding_{};
  bool initialized_{false};
  bool active_agent_binding_ready_{false};
  bool agent_slots_dirty_{false};
  std::function<void()> draw_callback_{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
};

}  // namespace agent_execute

using agent_execute::AgentExecuteRuntime;
using agent_execute::CreateCameraAgentInfo;
using agent_execute::CreatePhysicsAgentInfo;
