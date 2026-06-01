#pragma once

#include "agents/agents.h"
#include "agents/physics_agent.h"
#include "common_data/interaction/interaction_state.h"
#include "common_data/mesh.h"
#include "orchestration_entity/entity.h"
#include "algorithm_library/algorithm_types.h"

#include <chrono>
#include <cstddef>
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
  EntityHandle LoadEntity(CreateEntityInfo info);
  bool UnloadEntity(EntityHandle handle);
  bool createentity(CreateEntityInfo info) { return LoadEntity(std::move(info)) != kInvalidEntityHandle; }
  bool destroyentity(EntityHandle handle) { return UnloadEntity(handle); }
  bool Tick();
  void Destroy();

  const Mesh& mesh() const { return mesh_; }
  Mesh& mutable_mesh() { return mesh_; }

 private:
  static MountedAgentKind ClassifyMountedAgent(const std::string& mounted_agent_name);
  bool MountRenderEntity(const std::shared_ptr<orchestration_entity::OrchestrationEntity>& entity);
  bool MountPhysicsEntity(const std::shared_ptr<orchestration_entity::OrchestrationEntity>& entity);
  void RefreshBindingsFromEntitySlots();
  InteractionUiState BuildInteractionUiState(float animation_time) const;

  Mesh mesh_{};
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
  std::chrono::steady_clock::time_point start_time_{};
  std::chrono::steady_clock::time_point last_frame_time_{};
};

}  // namespace entity_interaction

using entity_interaction::CreateEntityInfo;
using entity_interaction::EntityInteractionRuntime;
using entity_interaction::EntityInteractionUiBridge;
