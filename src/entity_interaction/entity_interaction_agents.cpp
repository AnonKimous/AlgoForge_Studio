#include "entity_interaction_agents.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace entity_interaction {

namespace {

bool IsRenderMountedAgentName(const std::string& mounted_agent_name) {
  return mounted_agent_name == "render" || mounted_agent_name == "render_agent" ||
         mounted_agent_name == "instance_render";
}

bool IsPhysicsMountedAgentName(const std::string& mounted_agent_name) {
  return mounted_agent_name == "physics" || mounted_agent_name == "physics_agent" ||
         mounted_agent_name == "instance_physics";
}

PhysSolverKind InferSolverKind(const PhysSolverConfig& config, const std::string& algorithm_name) {
  if (algorithm_name == kPhysicsConvolutionGpuAlgorithmName || config.algorithm_name == kPhysicsConvolutionGpuAlgorithmName) {
    return PhysSolverKind::Gpu;
  }
  if (config.solver_kind == PhysSolverKind::Gpu) {
    return PhysSolverKind::Gpu;
  }
  if (config.solver_kind == PhysSolverKind::Cpu) {
    return PhysSolverKind::Cpu;
  }
  return PhysSolverKind::Cpu;
}

std::shared_ptr<orchestration_entity::OrchestrationEntity> FindFirstMountedEntity(
  const std::vector<std::shared_ptr<orchestration_entity::OrchestrationEntity>>& slots,
  MountedAgentKind kind) {
  for (const auto& slot : slots) {
    if (!slot) {
      continue;
    }
    const std::string& mounted_agent_name = slot->mounted_agent_name();
    switch (kind) {
      case MountedAgentKind::Render:
        if (IsRenderMountedAgentName(mounted_agent_name)) return slot;
        break;
      case MountedAgentKind::Physics:
        if (IsPhysicsMountedAgentName(mounted_agent_name)) return slot;
        break;
      case MountedAgentKind::None:
      default:
        break;
    }
  }
  return {};
}

}  // namespace

CreateEntityInfo CreateCameraEntityInfo(const Mesh& mesh) {
  CreateEntityInfo info{};
  info.algorithm_name = kCameraAlgorithmName;
  info.mounted_agent_name = "render_agent";
  info.bound_resources = {"mesh", "camera"};

  auto codec = std::make_shared<CameraAlgorithmPackageCodec>();
  codec::VolumeDescriptor volume{};
  volume.point_position = mesh.positions;
  volume.point_velocity.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  volume.mass = 1.0f;
  volume.driving_dir = Vec3{0.0f, 0.0f, 1.0f};

  AlgorithmComplianceDescriptor compliance_descriptor{};
  codec->BuildComplianceDescriptor(volume, &compliance_descriptor);
  info.compliance_packages.push_back(OrchestrationEntityAlgorithmPackageHandle{
    kCameraAlgorithmName,
    codec});
  info.compliance_descriptor = compliance_descriptor;
  return info;
}

InteractionUiState EntityInteractionUiBridge::BuildInteractionUiStateFromRenderAgentAndPhysicsAgent(
  const agents::RenderAgent& render_agent,
  const agents::PhysicsAgent& physics_agent,
  float animation_time) {
  InteractionUiState ui{};
  ui.mode = render_agent.mode();
  ui.phys_run_state = render_agent.phys_run_state();
  ui.phys_solver_kind = physics_agent.solver_kind();
  ui.phys_algorithm_name = physics_agent.algorithm_name();
  ui.phys_current_frame_index = physics_agent.current_frame_index();
  ui.total_velocities = physics_agent.total_velocities();
  ui.linear_velocities = physics_agent.linear_velocities();
  ui.angular_velocities = physics_agent.angular_velocities();
  ui.gpu_dispatch_debug = physics_agent.gpu_dispatch_debug_info();
  ui.agent_to_algorithm_signal = physics_agent.agent_to_algorithm_signal();
  ui.algorithm_to_agent_signal = physics_agent.algorithm_to_agent_signal();
  ui.intervention_request = physics_agent.intervention_request();
  ui.animation_time = animation_time;
  return ui;
}

void EntityInteractionUiBridge::ApplyInteractionUiActionOnPhysicsAgentAndMesh(
  const InteractionUiAction& action,
  agents::PhysicsAgent* physics_agent,
  Mesh* mesh) {
  if (!physics_agent || !mesh) {
    return;
  }
  physics_agent->ApplyUiAction(action, *mesh);
}

bool EntityInteractionRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  initialized_ = window_agent_.Init(window_title ? window_title : "Entity Interaction", width, height);
  if (!initialized_) {
    return false;
  }
  start_time_ = std::chrono::steady_clock::now();
  last_frame_time_ = start_time_;
  return true;
}

MountedAgentKind EntityInteractionRuntime::ClassifyMountedAgent(
  const std::string& mounted_agent_name) {
  if (IsRenderMountedAgentName(mounted_agent_name)) {
    return MountedAgentKind::Render;
  }
  if (IsPhysicsMountedAgentName(mounted_agent_name)) {
    return MountedAgentKind::Physics;
  }
  return MountedAgentKind::None;
}

bool EntityInteractionRuntime::MountRenderEntity(const std::shared_ptr<orchestration_entity::OrchestrationEntity>& entity) {
  if (render_entity_ == entity && render_entity_ready_) {
    return true;
  }
  if (!entity) {
    if (render_entity_ready_) {
      render_agent_.Destroy();
    }
    render_entity_.reset();
    render_entity_ready_ = false;
    return true;
  }

  if (render_entity_ready_) {
    render_agent_.Destroy();
    render_entity_ready_ = false;
  }

  render_entity_ = entity;
  render_agent_.Init(window_agent_.native_handle());
  render_entity_ready_ = true;
  return true;
}

bool EntityInteractionRuntime::MountPhysicsEntity(const std::shared_ptr<orchestration_entity::OrchestrationEntity>& entity) {
  if (physics_entity_ == entity && physics_entity_ready_) {
    return true;
  }
  if (!entity) {
    if (physics_entity_ready_) {
      physics_agent_.Destroy();
    }
    physics_entity_.reset();
    physics_entity_ready_ = false;
    return true;
  }

  physics_entity_ = entity;
  if (physics_entity_ready_) {
    physics_agent_.Destroy();
    physics_entity_ready_ = false;
  }

  PhysSolverConfig solver_config = entity->solver_config();
  if (solver_config.algorithm_name.empty()) {
    solver_config.algorithm_name = entity->algorithm_name();
  }
  solver_config.solver_kind = InferSolverKind(solver_config, entity->algorithm_name());
  physics_agent_.Init(solver_config, VulkanComputeContextView{}, entity->compliance_descriptor());
  if (entity->intervention_package()) {
    physics_agent_.SetInterventionPackage(entity->intervention_package());
  }
  physics_entity_ready_ = true;
  return true;
}

EntityInteractionRuntime::EntityHandle EntityInteractionRuntime::LoadEntity(CreateEntityInfo info) {
  auto entity = std::make_shared<orchestration_entity::OrchestrationEntity>();
  if (!orchestration_entity_init(entity.get(), std::move(info))) {
    return kInvalidEntityHandle;
  }

  const EntityHandle handle = entity_slots_.size();
  entity_slots_.push_back(entity);
  entity_slots_dirty_ = true;
  return handle;
}

bool EntityInteractionRuntime::UnloadEntity(EntityHandle handle) {
  if (handle >= entity_slots_.size() || !entity_slots_[handle]) {
    return false;
  }
  entity_slots_[handle].reset();
  entity_slots_dirty_ = true;
  return true;
}

void EntityInteractionRuntime::RefreshBindingsFromEntitySlots() {
  auto desired_render = FindFirstMountedEntity(entity_slots_, MountedAgentKind::Render);
  auto desired_physics = FindFirstMountedEntity(entity_slots_, MountedAgentKind::Physics);

  MountRenderEntity(desired_render);
  MountPhysicsEntity(desired_physics);
}

InteractionUiState EntityInteractionRuntime::BuildInteractionUiState(float animation_time) const {
  return EntityInteractionUiBridge::BuildInteractionUiStateFromRenderAgentAndPhysicsAgent(
    render_agent_,
    physics_agent_,
    animation_time);
}

bool EntityInteractionRuntime::Tick() {
  if (!initialized_) {
    return false;
  }
  if (!window_agent_.Tick()) {
    return false;
  }

  if (entity_slots_dirty_) {
    RefreshBindingsFromEntitySlots();
    entity_slots_dirty_ = false;
  }

  const auto now = std::chrono::steady_clock::now();
  float frame_dt = std::chrono::duration<float>(now - last_frame_time_).count();
  frame_dt = std::clamp(frame_dt, 0.0f, 0.05f);
  last_frame_time_ = now;

  if (physics_entity_ready_) {
    physics_agent_.SetRunState(render_agent_.phys_run_state());
    physics_agent_.Tick(
      mesh_,
      window_agent_.input(),
      window_agent_.MousePosition(),
      frame_dt);
  }

  if (render_entity_ready_ && physics_entity_ready_) {
    InteractionUiState ui = BuildInteractionUiState(std::chrono::duration<float>(now - start_time_).count());
    InteractionUiAction frame = render_agent_.Tick(mesh_, ui);
    EntityInteractionUiBridge::ApplyInteractionUiActionOnPhysicsAgentAndMesh(frame, &physics_agent_, &mesh_);
  }

  return true;
}

void EntityInteractionRuntime::Destroy() {
  physics_agent_.Destroy();
  render_agent_.Destroy();
  physics_entity_.reset();
  render_entity_.reset();
  entity_slots_.clear();
  window_agent_.Destroy();
  mesh_ = Mesh{};
  initialized_ = false;
  render_entity_ready_ = false;
  physics_entity_ready_ = false;
  entity_slots_dirty_ = false;
  start_time_ = {};
  last_frame_time_ = {};
}

}  // namespace entity_interaction
