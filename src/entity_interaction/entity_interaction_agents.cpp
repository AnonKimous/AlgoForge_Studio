#include "entity_interaction_agents.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"
#include "algorithm_library/random_vertex_motion_algorithm_contract.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <array>
#include <filesystem>
#include <imgui.h>
#include <cstdio>
#include <iterator>
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
    case 3:
      return "render_physics_agent";
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
  codec->BuildComplianceDescriptor(volume, &compliance_descriptor);
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
    info.compliance_descriptor = CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor(
      static_cast<uint32_t>(mesh.positions.size()));
  } else {
    info.compliance_descriptor = CreateCorotatedCpuAlgorithmComplianceDescriptor(
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

CreateEntityInfo BuildRandomVertexMotionEntityInfoFromMesh(
  const Mesh& mesh,
  float motion_radius,
  const std::string& mounted_agent_name,
  const std::string& instance_name) {
  CreateEntityInfo info{};
  info.instance_name = instance_name.empty() ? "random_vertex_motion_instance" : instance_name;
  info.algorithm_name = kRandomVertexMotionAlgorithmName;
  info.mounted_agent_name = mounted_agent_name.empty() ? "render_physics_agent" : mounted_agent_name;
  info.bound_resources = {"mesh", "vertex_positions", "triangle_edges", "motion_radius"};
  info.solver_config.solver_kind = PhysSolverKind::Cpu;
  info.solver_config.algorithm_name = kRandomVertexMotionAlgorithmName;
  info.solver_config.run_algorithm_on_init = true;
  info.compliance_descriptor = CreateRandomVertexMotionAlgorithmComplianceDescriptor(mesh, motion_radius);
  info.compliance_packages.push_back(OrchestrationEntityAlgorithmPackageHandle{
    kRandomVertexMotionAlgorithmName,
    nullptr});
  info.intervention_package = std::make_shared<OrchestrationEntityInterventionPackageHandle>();
  info.intervention_package->package_name = "random_vertex_motion_intervention";
  info.intervention_package->codec_hook = nullptr;
  info.intervention_package->agent_hook = nullptr;
  info.intervention_package->algorithm_hook = nullptr;
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
  const std::string& mounted_agent_name) {
  return BuildPhysicsEntityInfoFromMesh(mesh, solver_kind, algorithm_name, std::string{}, mounted_agent_name, "");
}

CreateEntityInfo CreateRandomVertexMotionEntityInfo(
  const Mesh& mesh,
  float motion_radius,
  const std::string& mounted_agent_name) {
  return BuildRandomVertexMotionEntityInfoFromMesh(mesh, motion_radius, mounted_agent_name, "");
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
  window_agent_.SetDrawCallback([this]() { DrawInstanceComposerUi(); });
  mesh_source_path_.clear();
  ResetInstanceDraftState();
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
    selected_entity_slot_ = static_cast<std::size_t>(-1);
    render_agent_.Destroy();
    physics_agent_.Destroy();
    ResetInstanceDraftState();
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

void EntityInteractionRuntime::ResetInstanceDraftState() {
  std::snprintf(instance_draft_.instance_name, sizeof(instance_draft_.instance_name), "%s", "entity_0");
  std::snprintf(instance_draft_.algorithm_name, sizeof(instance_draft_.algorithm_name), "%s", kCorotatedCpuAlgorithmName);
  std::snprintf(instance_draft_.mesh_path, sizeof(instance_draft_.mesh_path), "%s", mesh_source_path_.c_str());
  instance_draft_.gpu_shader_path[0] = '\0';
  instance_draft_.motion_radius = 0.15f;
  instance_draft_.preset_index = 3;
  instance_draft_.solver_kind_index = 0;
  instance_draft_.load_mesh_before_create = true;
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

bool EntityInteractionRuntime::CreateEntityFromDraft(std::string* status_message) {
  std::string mesh_path = instance_draft_.mesh_path;
  const bool should_load_mesh = instance_draft_.load_mesh_before_create && !mesh_path.empty() && mesh_path != mesh_source_path_;
  if (should_load_mesh) {
    if (!LoadMeshAndResetBindings(mesh_path, status_message)) {
      return false;
    }
  }

  CreateEntityInfo info{};
  const std::string instance_name = instance_draft_.instance_name;
  const std::string algorithm_name = instance_draft_.algorithm_name;
  const int preset_index = std::clamp(instance_draft_.preset_index, 0, 3);
  PhysSolverConfig draft_solver{};
  draft_solver.solver_kind = instance_draft_.solver_kind_index == 1 ? PhysSolverKind::Gpu : PhysSolverKind::Cpu;
  draft_solver.algorithm_name = algorithm_name;
  const PhysSolverKind solver_kind = InferSolverKind(draft_solver, algorithm_name);
  if (solver_kind == PhysSolverKind::Gpu && std::string(instance_draft_.gpu_shader_path).empty()) {
    if (status_message) {
      *status_message = "GPU preset needs a shader path.";
    }
    return false;
  }

  if (preset_index == 0) {
    info = BuildCameraEntityInfoFromMesh(mesh_, ResolveMountedAgentNameForPreset(preset_index), instance_name);
    const EntityHandle handle = LoadEntity(std::move(info));
    if (handle == kInvalidEntityHandle) {
      if (status_message) {
      *status_message = "Failed to create camera entity.";
      }
      return false;
    }
    if (status_message) {
      *status_message = "Created camera entity slot " + std::to_string(handle);
    }
    return true;
  }

  if (preset_index == 3) {
    info = BuildRandomVertexMotionEntityInfoFromMesh(
      mesh_,
      instance_draft_.motion_radius,
      ResolveMountedAgentNameForPreset(preset_index),
      instance_name);
    const EntityHandle handle = LoadEntity(std::move(info));
    if (handle == kInvalidEntityHandle) {
      if (status_message) {
        *status_message = "Failed to create shared random-motion entity.";
      }
      return false;
    }
    if (status_message) {
      *status_message = "Created shared random-motion entity slot " + std::to_string(handle) + " and started it.";
    }
    return true;
  }

  {
    info = BuildPhysicsEntityInfoFromMesh(
      mesh_,
      solver_kind,
      algorithm_name,
      instance_draft_.gpu_shader_path,
      ResolveMountedAgentNameForPreset(preset_index),
      instance_name);
    const EntityHandle handle = LoadEntity(std::move(info));
    if (handle == kInvalidEntityHandle) {
      if (status_message) {
        *status_message = "Failed to create entity.";
      }
      return false;
    }
    if (status_message) {
      *status_message = "Created entity slot " + std::to_string(handle);
    }
    return true;
  }
}

void EntityInteractionRuntime::DrawLoadedEntityListUi() {
  ImGui::Text("Loaded entities: %zu", entity_slots_.size());
  ImGui::Separator();

  if (ImGui::BeginListBox("##entity_list", ImVec2(-FLT_MIN, 220.0f))) {
    for (std::size_t index = 0; index < entity_slots_.size(); ++index) {
      const auto& entity = entity_slots_[index];
      if (!entity) {
        continue;
      }

      std::string label = "[" + std::to_string(index) + "] ";
      label += entity->instance_name().empty() ? entity->algorithm_name() : entity->instance_name();
      label += " | ";
      label += entity->mounted_agent_name();
      if (IsSharedMountedAgentName(entity->mounted_agent_name())) {
        label += " [shared]";
      }
      label += " | ";
      label += entity->algorithm_name();

      const bool selected = (selected_entity_slot_ == index);
      if (ImGui::Selectable(label.c_str(), selected)) {
        selected_entity_slot_ = index;
      }
    }
    ImGui::EndListBox();
  }

  const bool has_selected = selected_entity_slot_ < entity_slots_.size() && entity_slots_[selected_entity_slot_];
  if (has_selected) {
    const auto& entity = entity_slots_[selected_entity_slot_];
    ImGui::Text("Selected: %s", entity->instance_name().empty() ? entity->algorithm_name().c_str() : entity->instance_name().c_str());
    ImGui::Text("Agent: %s", entity->mounted_agent_name().c_str());
    if (IsSharedMountedAgentName(entity->mounted_agent_name())) {
      ImGui::TextUnformatted("Role: shared render + physics");
    }
    ImGui::Text("Algorithm: %s", entity->algorithm_name().c_str());
    if (ImGui::Button("Unload Selected")) {
      UnloadEntity(selected_entity_slot_);
      selected_entity_slot_ = static_cast<std::size_t>(-1);
    }
  } else {
    ImGui::TextUnformatted("No entity selected.");
  }
}

void EntityInteractionRuntime::DrawInstanceDraftUi() {
  static constexpr const char* kSolverLabels[] = {
    "CPU",
    "GPU",
  };
  static constexpr float kRandomMotionRadiusMax = 100.0f;

  ImGui::InputText("Entity Name", instance_draft_.instance_name, sizeof(instance_draft_.instance_name));
  ImGui::InputText("Mesh File", instance_draft_.mesh_path, sizeof(instance_draft_.mesh_path));
  if (instance_draft_.motion_radius <= 0.0f) {
    instance_draft_.motion_radius = 0.15f;
  }
  ImGui::SliderFloat("Motion Radius", &instance_draft_.motion_radius, 0.01f, kRandomMotionRadiusMax, "%.3f");
  ImGui::Text("Random drift radius range: 0.01 .. %.0f", kRandomMotionRadiusMax);

  if (ImGui::Button("Create and Run Shared Entity")) {
    instance_draft_.preset_index = 3;
    std::snprintf(instance_draft_.algorithm_name, sizeof(instance_draft_.algorithm_name), "%s", kRandomVertexMotionAlgorithmName);
    std::string message;
    if (CreateEntityFromDraft(&message)) {
      ui_status_message_ = message;
    } else {
      ui_status_message_ = message.empty() ? "Failed to create shared entity." : message;
    }
  }

  ImGui::TextUnformatted("This preset creates one entity that mounts both render and physics roles, then starts it on the next tick.");

  if (ImGui::CollapsingHeader("Advanced Presets")) {
    ImGui::Combo("Solver", &instance_draft_.solver_kind_index, kSolverLabels, static_cast<int>(std::size(kSolverLabels)));
    if (instance_draft_.solver_kind_index == 1) {
      ImGui::InputText("GPU Shader", instance_draft_.gpu_shader_path, sizeof(instance_draft_.gpu_shader_path));
    }

    if (ImGui::Button("Create Camera Entity")) {
      instance_draft_.preset_index = 0;
      std::snprintf(instance_draft_.algorithm_name, sizeof(instance_draft_.algorithm_name), "%s", kCameraAlgorithmName);
      std::string message;
      if (CreateEntityFromDraft(&message)) {
        ui_status_message_ = message;
      } else {
        ui_status_message_ = message.empty() ? "Failed to create camera entity." : message;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Physics Entity")) {
      instance_draft_.preset_index = instance_draft_.solver_kind_index == 1 ? 2 : 1;
      std::snprintf(
        instance_draft_.algorithm_name,
        sizeof(instance_draft_.algorithm_name),
        "%s",
        instance_draft_.solver_kind_index == 1 ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName);
      std::string message;
      if (CreateEntityFromDraft(&message)) {
        ui_status_message_ = message;
      } else {
        ui_status_message_ = message.empty() ? "Failed to create physics entity." : message;
      }
    }
  }

  if (ImGui::Button("Load Mesh")) {
    std::string message;
    if (LoadMeshAndResetBindings(instance_draft_.mesh_path, &message)) {
      ui_status_message_ = message;
    } else {
      ui_status_message_ = message.empty() ? "Failed to load mesh." : message;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Entity Form")) {
    ResetInstanceDraftState();
      ui_status_message_ = "Entity form reset.";
  }
}

void EntityInteractionRuntime::DrawInstanceComposerUi() {
  const auto& vertex_positions = physics_entity_ready_ ? physics_agent_.vertex_positions() : mesh_.positions;
  agents::DrawVertexArrayOverlay(vertex_positions, mesh_.edges, mesh_.triangles);
  ImGui::Begin("Entity Interaction");
  ImGui::Text("Mesh source: %s", mesh_source_path_.empty() ? "<in-memory>" : mesh_source_path_.c_str());
  ImGui::TextUnformatted("Rendering the algorithm vertex array, not the source mesh object.");
  ImGui::Text("Status: %s", ui_status_message_.empty() ? "ready" : ui_status_message_.c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Create Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawInstanceDraftUi();
  }

  if (ImGui::CollapsingHeader("Loaded Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLoadedEntityListUi();
  }

  ImGui::Separator();
  ImGui::Text("Active render entity: %s", render_entity_ready_ ? render_entity_->instance_name().c_str() : "<none>");
  ImGui::Text("Active physics entity: %s", physics_entity_ready_ ? physics_entity_->instance_name().c_str() : "<none>");
  if (render_entity_ready_ && physics_entity_ready_ && render_entity_ == physics_entity_) {
    ImGui::Text("Active shared entity: %s", render_entity_->instance_name().c_str());
  }
  ImGui::End();
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

EntityInteractionRuntime::EntityHandle EntityInteractionRuntime::LoadEntity(CreateEntityInfo info) {
  auto entity = std::make_shared<orchestration_entity::OrchestrationEntity>();
  if (!orchestration_entity_init(entity.get(), std::move(info))) {
    return kInvalidEntityHandle;
  }

  const EntityHandle handle = entity_slots_.size();
  entity_slots_.push_back(entity);
  entity_slots_dirty_ = true;
  selected_entity_slot_ = handle;
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
  selected_entity_slot_ = static_cast<std::size_t>(-1);
  ui_status_message_.clear();
  ResetInstanceDraftState();
  start_time_ = {};
  last_frame_time_ = {};
}

}  // namespace entity_interaction
