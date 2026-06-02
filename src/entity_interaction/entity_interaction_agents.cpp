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
         mounted_agent_name == "instance_render" || mounted_agent_name == "render_physics_agent" ||
         mounted_agent_name == "dual_agent";
}

bool IsPhysicsMountedAgentName(const std::string& mounted_agent_name) {
  return mounted_agent_name == "physics" || mounted_agent_name == "physics_agent" ||
         mounted_agent_name == "instance_physics" || mounted_agent_name == "render_physics_agent" ||
         mounted_agent_name == "dual_agent";
}

bool IsSharedMountedAgentName(const std::string& mounted_agent_name) {
  return mounted_agent_name == "render_physics_agent" || mounted_agent_name == "dual_agent";
}

const char* ResolveMountedAgentNameForPreset(int preset_index) {
  switch (preset_index) {
    case 0:
      return "render_agent";
    case 1:
    case 2:
      return "physics_agent";
    default:
      return "physics_agent";
  }
}

CreateEntityInfo BuildCameraEntityInfoFromMesh(const Mesh& mesh, const std::string& mounted_agent_name, const std::string& instance_name) {
  CreateEntityInfo info{};
  info.instance_name = instance_name.empty() ? "camera_instance" : instance_name;
  info.algorithm_name = kCameraAlgorithmName;
  info.mounted_agent_name = mounted_agent_name.empty() ? "render_agent" : mounted_agent_name;
  info.bound_resources = {"mesh", "camera"};

  auto codec = std::make_shared<CameraAlgorithmPackageCodec>();
  codec::VolumeDescriptor volume{};
  volume.point_position = mesh.positions;
  volume.point_velocity.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  volume.mass = 1.0f;
  volume.driving_dir = Vec3{0.0f, 0.0f, 1.0f};

  AlgorithmComplianceDescriptor compliance_descriptor{};
  codec->BuildContainerDescriptor(volume, &compliance_descriptor);
  info.compliance_packages.push_back(OrchestrationEntityAlgorithmPackageHandle{
    kCameraAlgorithmName,
    codec});
  info.compliance_descriptor = compliance_descriptor;
  return info;
}

CreateEntityInfo BuildPhysicsEntityInfoFromMesh(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path,
  const std::string& mounted_agent_name,
  const std::string& instance_name) {
  CreateEntityInfo info{};
  info.instance_name = instance_name.empty() ? (algorithm_name.empty() ? "physics_instance" : algorithm_name + "_instance") : instance_name;
  info.algorithm_name = algorithm_name.empty()
    ? (solver_kind == PhysSolverKind::Gpu ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName)
    : algorithm_name;
  info.mounted_agent_name = mounted_agent_name.empty() ? "physics_agent" : mounted_agent_name;
  info.bound_resources = {"mesh", "physics_state", "compute_context"};

  PhysSolverConfig solver_config{};
  solver_config.solver_kind = solver_kind;
  solver_config.algorithm_name = info.algorithm_name;
  solver_config.run_algorithm_on_init = true;
  if (solver_kind == PhysSolverKind::Gpu) {
    solver_config.gpu_shader.shader_name = gpu_shader_path;
    solver_config.gpu_shader.shader_mask.assign(9, 1u);
    solver_config.gpu_shader.shader_data.assign(9, 1.0f);
  }
  info.solver_config = solver_config;

  if (solver_kind == PhysSolverKind::Gpu) {
    info.compliance_descriptor = CreatePhysicsConvolutionGpuAlgorithmContainerDescriptor(
      static_cast<uint32_t>(mesh.positions.size()));
  } else {
    info.compliance_descriptor = CreateCorotatedCpuAlgorithmContainerDescriptor(
      static_cast<uint32_t>(mesh.positions.size()),
      static_cast<uint32_t>(mesh.triangles.size()));
  }
  info.compliance_packages.push_back(OrchestrationEntityAlgorithmPackageHandle{
    info.algorithm_name,
    nullptr});
  info.intervention_package = std::make_shared<OrchestrationEntityInterventionPackageHandle>();
  info.intervention_package->package_name = "physics_intervention";
  return info;
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
  for (auto it = slots.rbegin(); it != slots.rend(); ++it) {
    const auto& slot = *it;
    if (!slot) continue;
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
  return BuildCameraEntityInfoFromMesh(mesh, "render_agent", "camera_instance");
}

CreateEntityInfo CreatePhysicsEntityInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& mounted_agent_name,
  const std::string& gpu_shader_path) {
  return BuildPhysicsEntityInfoFromMesh(mesh, solver_kind, algorithm_name, gpu_shader_path, mounted_agent_name, "");
}

InteractionUiState EntityInteractionUiBridge::BuildInteractionUiStateFromRenderAgentAndPhysicsAgent(
  const agents::RenderAgent& render_agent,
  const agents::PhysicsAgent& physics_agent,
  float animation_time) {
  (void)physics_agent;
  InteractionUiState ui{};
  ui.mode = render_agent.mode();
  ui.phys_run_state = render_agent.phys_run_state();
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
  physics_agent->SetRunState(action.phys_run_state);
  physics_agent->SetAgentToAlgorithmSignal(action.agent_to_algorithm_signal);
  if (action.intervention_request.enabled) {
    physics_agent->ApplyInterventionRequest(action.intervention_request);
  }
  if (action.phys_reset_requested) {
    physics_agent->Reset(*mesh);
  }
  if (action.phys_step_requested) {
    physics_agent->StepOnce(*mesh);
  }
}

bool EntityInteractionRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  initialized_ = window_agent_.Init(window_title ? window_title : "Entity Interaction", width, height);
  if (!initialized_) {
    return false;
  }
  mesh_source_path_.clear();
  ui_status_message_ = "Mesh is prefilled. Create the shared entity to start work.";
  start_time_ = std::chrono::steady_clock::now();
  last_frame_time_ = start_time_;
  return true;
}

bool EntityInteractionRuntime::LoadMeshFromFile(const std::string& path, std::string* error_message) {
  try {
    mesh_ = LoadMeshFile(path);
    mesh_source_path_ = path;
    entity_slots_.clear();
    render_entity_.reset();
    physics_entity_.reset();
    render_entity_ready_ = false;
    physics_entity_ready_ = false;
    entity_slots_dirty_ = false;
    render_agent_.Destroy();
    physics_agent_.Destroy();
    ui_status_message_ = "Loaded mesh: " + path;
    return true;
  } catch (const std::exception& error) {
    if (error_message) {
      *error_message = error.what();
    }
    ui_status_message_ = std::string("Mesh load failed: ") + error.what();
    return false;
  }
}

void EntityInteractionRuntime::SetDrawCallback(std::function<void()> draw_callback) {
  draw_callback_ = std::move(draw_callback);
  window_agent_.SetDrawCallback(draw_callback_);
}

bool EntityInteractionRuntime::LoadMeshAndResetBindings(const std::string& path, std::string* status_message) {
  std::string error_message;
  if (!LoadMeshFromFile(path, &error_message)) {
    if (status_message) {
      *status_message = error_message;
    }
    return false;
  }
  if (status_message) {
    *status_message = "Loaded mesh and cleared existing entities.";
  }
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
  render_agent_.SetPhysRunState(IsSharedMountedAgentName(entity->mounted_agent_name()) ? PhysRunState::Run : PhysRunState::Pause);
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
  physics_agent_.SetRunState(IsSharedMountedAgentName(entity->mounted_agent_name()) ? PhysRunState::Run : PhysRunState::Pause);
  physics_entity_ready_ = true;
  return true;
}

const std::vector<Vec3>& EntityInteractionRuntime::active_vertex_positions() const {
  return mesh_.positions;
}

void EntityInteractionRuntime::SetUiStatusMessage(std::string message) {
  ui_status_message_ = std::move(message);
}

InteractionUiState EntityInteractionRuntime::BuildInteractionUiState(float animation_time) const {
  return EntityInteractionUiBridge::BuildInteractionUiStateFromRenderAgentAndPhysicsAgent(
    render_agent_,
    physics_agent_,
    animation_time);
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

  if (render_entity_ready_ && physics_entity_ready_ && render_entity_ == physics_entity_) {
    render_agent_.SetPhysRunState(PhysRunState::Run);
    physics_agent_.SetRunState(PhysRunState::Run);
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
  mesh_source_path_.clear();
  initialized_ = false;
  render_entity_ready_ = false;
  physics_entity_ready_ = false;
  entity_slots_dirty_ = false;
  ui_status_message_.clear();
  draw_callback_ = {};
  start_time_ = {};
  last_frame_time_ = {};
}

}  // namespace entity_interaction
