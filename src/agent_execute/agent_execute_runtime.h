#pragma once

#include "agents/agents.h"
#include "agents/physics_agent.h"
#include "common_data/interaction/interaction_state.h"
#include "common_data/mesh.h"
#include "agent/agent.h"
#include "algorithm_library/algorithm_types.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace agent_execute {

enum class MountedRuntimeKind {
  None,
  Render,
  Physics,
};

struct CreateAgentInfo : agent::AgentInitConfig {
  using agent::AgentInitConfig::AgentInitConfig;
};

CreateAgentInfo CreateCameraAgentInfo(const Mesh& mesh);
CreateAgentInfo CreatePhysicsAgentInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& mounted_agent_name = "physics_agent",
  const std::string& gpu_shader_path = "");

class AgentUiBridge {
 public:
  static InteractionUiState BuildInteractionUiStateFromRenderAgentAndPhysicsAgent(
    const agent_execute::RenderAgent& render_agent,
    const agent_execute::PhysicsAgent& physics_agent,
    float animation_time);

  static void ApplyInteractionUiActionOnPhysicsAgentAndMesh(
    const InteractionUiAction& action,
    agent_execute::PhysicsAgent* physics_agent,
    Mesh* mesh);
};

class AgentExecuteRuntime {
 public:
  using AgentHandle = std::size_t;
  static constexpr AgentHandle kInvalidAgentHandle = static_cast<AgentHandle>(-1);

  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool LoadMeshFromFile(const std::string& path, std::string* error_message = nullptr);
  AgentHandle LoadAgent(CreateAgentInfo info);
  bool UnloadAgent(AgentHandle handle);
  void SetDrawCallback(std::function<void()> draw_callback);
  bool createagent(CreateAgentInfo info) { return LoadAgent(std::move(info)) != kInvalidAgentHandle; }
  bool destroyagent(AgentHandle handle) { return UnloadAgent(handle); }
  bool Tick();
  void Destroy();

  const Mesh& mesh() const { return mesh_; }
  Mesh& mutable_mesh() { return mesh_; }
  const std::string& mesh_source_path() const { return mesh_source_path_; }
  const std::vector<std::shared_ptr<agent::Agent>>& agent_slots() const { return agent_slots_; }
  const std::shared_ptr<agent::Agent>& render_agent_binding() const { return render_agent_binding_; }
  const std::shared_ptr<agent::Agent>& physics_agent_binding() const { return physics_agent_binding_; }
  bool render_agent_binding_ready() const { return render_agent_binding_ready_; }
  bool physics_agent_binding_ready() const { return physics_agent_binding_ready_; }
  const std::vector<Vec3>& active_vertex_positions() const;
  const std::string& ui_status_message() const { return ui_status_message_; }
  void SetUiStatusMessage(std::string message);
  InteractionUiState BuildInteractionUiState(float animation_time) const;

 private:
  enum class AgentPresetKind {
    Camera,
    PhysicsCpu,
    PhysicsGpu,
  };

  bool LoadMeshAndResetBindings(const std::string& path, std::string* status_message);
  static MountedRuntimeKind ClassifyMountedAgent(const std::string& mounted_agent_name);
  bool MountRenderAgent(const std::shared_ptr<agent::Agent>& agent);
  bool MountPhysicsAgent(const std::shared_ptr<agent::Agent>& agent);
  void RefreshBindingsFromAgentSlots();

  Mesh mesh_{};
  std::string mesh_source_path_{};
  agent_execute::WindowAgent window_agent_{};
  agent_execute::RenderAgent render_agent_{};
  agent_execute::PhysicsAgent physics_agent_{};
  std::vector<std::shared_ptr<agent::Agent>> agent_slots_{};
  std::shared_ptr<agent::Agent> render_agent_binding_{};
  std::shared_ptr<agent::Agent> physics_agent_binding_{};
  bool initialized_{false};
  bool render_agent_binding_ready_{false};
  bool physics_agent_binding_ready_{false};
  bool agent_slots_dirty_{false};
  std::function<void()> draw_callback_{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point start_time_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
};

}  // namespace agent_execute

using agent_execute::AgentExecuteRuntime;
using agent_execute::CreateAgentInfo;
using agent_execute::CreateCameraAgentInfo;
using agent_execute::CreatePhysicsAgentInfo;
using agent_execute::AgentUiBridge;
