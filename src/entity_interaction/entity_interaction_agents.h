#pragma once

#include "agents/agents.h"
#include "agents/physics_agent.h"
#include "common_data/interaction/interaction_state.h"
#include "common_data/mesh.h"
#include "orchestration_entity/entity.h"
#include "algorithm_library/algorithm_types.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace entity_interaction {

enum class MountedAgentKind {
  None,
  Render,
  Physics,
};

struct CreateEntityInfo : orchestration_entity::OrchestrationEntityInitConfig {
  using orchestration_entity::OrchestrationEntityInitConfig::OrchestrationEntityInitConfig;
};

CreateEntityInfo CreateCameraEntityInfo(const Mesh& mesh);
CreateEntityInfo CreatePhysicsEntityInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& mounted_agent_name = "physics_agent",
  const std::string& gpu_shader_path = "");

class EntityInteractionUiBridge {
 public:
  static InteractionUiState BuildInteractionUiStateFromRenderAgentAndPhysicsAgent(
    const agents::RenderAgent& render_agent,
    const agents::PhysicsAgent& physics_agent,
    float animation_time);

  static void ApplyInteractionUiActionOnPhysicsAgentAndMesh(
    const InteractionUiAction& action,
    agents::PhysicsAgent* physics_agent,
    Mesh* mesh);
};

class EntityInteractionRuntime {
 public:
  using EntityHandle = std::size_t;
  static constexpr EntityHandle kInvalidEntityHandle = static_cast<EntityHandle>(-1);

  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool LoadMeshFromFile(const std::string& path, std::string* error_message = nullptr);
  EntityHandle LoadEntity(CreateEntityInfo info);
  bool UnloadEntity(EntityHandle handle);
  void SetDrawCallback(std::function<void()> draw_callback);
  bool createentity(CreateEntityInfo info) { return LoadEntity(std::move(info)) != kInvalidEntityHandle; }
  bool destroyentity(EntityHandle handle) { return UnloadEntity(handle); }
  bool Tick();
  void Destroy();

  const Mesh& mesh() const { return mesh_; }
  Mesh& mutable_mesh() { return mesh_; }
  const std::string& mesh_source_path() const { return mesh_source_path_; }
  const std::vector<std::shared_ptr<orchestration_entity::OrchestrationEntity>>& entity_slots() const { return entity_slots_; }
  const std::shared_ptr<orchestration_entity::OrchestrationEntity>& render_entity() const { return render_entity_; }
  const std::shared_ptr<orchestration_entity::OrchestrationEntity>& physics_entity() const { return physics_entity_; }
  bool render_entity_ready() const { return render_entity_ready_; }
  bool physics_entity_ready() const { return physics_entity_ready_; }
  const std::vector<Vec3>& active_vertex_positions() const;
  const std::string& ui_status_message() const { return ui_status_message_; }
  void SetUiStatusMessage(std::string message);
  InteractionUiState BuildInteractionUiState(float animation_time) const;

 private:
  enum class InstancePresetKind {
    Camera,
    PhysicsCpu,
    PhysicsGpu,
  };

  bool LoadMeshAndResetBindings(const std::string& path, std::string* status_message);
  static MountedAgentKind ClassifyMountedAgent(const std::string& mounted_agent_name);
  bool MountRenderEntity(const std::shared_ptr<orchestration_entity::OrchestrationEntity>& entity);
  bool MountPhysicsEntity(const std::shared_ptr<orchestration_entity::OrchestrationEntity>& entity);
  void RefreshBindingsFromEntitySlots();

  Mesh mesh_{};
  std::string mesh_source_path_{};
  agents::WindowAgent window_agent_{};
  agents::RenderAgent render_agent_{};
  agents::PhysicsAgent physics_agent_{};
  std::vector<std::shared_ptr<orchestration_entity::OrchestrationEntity>> entity_slots_{};
  std::shared_ptr<orchestration_entity::OrchestrationEntity> render_entity_{};
  std::shared_ptr<orchestration_entity::OrchestrationEntity> physics_entity_{};
  bool initialized_{false};
  bool render_entity_ready_{false};
  bool physics_entity_ready_{false};
  bool entity_slots_dirty_{false};
  std::function<void()> draw_callback_{};
  std::string ui_status_message_{};
  std::chrono::steady_clock::time_point start_time_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
};

}  // namespace entity_interaction

using entity_interaction::CreateEntityInfo;
using entity_interaction::CreateCameraEntityInfo;
using entity_interaction::CreatePhysicsEntityInfo;
using entity_interaction::EntityInteractionRuntime;
using entity_interaction::EntityInteractionUiBridge;
