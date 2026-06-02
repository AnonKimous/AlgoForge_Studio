#include "editor_ui_runtime.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <imgui.h>
#include <iterator>
#include <utility>

namespace editor_ui {

namespace {

const char* ResolveMountedAgentNameForPreset(int preset_index) {
  switch (preset_index) {
    case 0:
      return "render_agent";
    case 1:
    case 2:
    default:
      return "physics_agent";
  }
}

}  // namespace

void EditorUiRuntime::ResetEntityDraftState() {
  std::snprintf(entity_draft_.entity_name, sizeof(entity_draft_.entity_name), "%s", "entity_0");
  std::snprintf(entity_draft_.algorithm_name, sizeof(entity_draft_.algorithm_name), "%s", kCorotatedCpuAlgorithmName);
  std::snprintf(entity_draft_.mesh_path, sizeof(entity_draft_.mesh_path), "%s", entity_runtime_.mesh_source_path().c_str());
  entity_draft_.gpu_shader_path[0] = '\0';
  entity_draft_.preset_index = 1;
  entity_draft_.solver_kind_index = 0;
  entity_draft_.load_mesh_before_create = true;
}

bool EditorUiRuntime::LoadMeshAndResetBindings(const std::string& path, std::string* status_message) {
  std::string error_message;
  if (!entity_runtime_.LoadMeshFromFile(path, &error_message)) {
    const std::string message = error_message.empty() ? "Failed to load mesh." : error_message;
    entity_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  ResetEntityDraftState();
  selected_entity_slot_ = static_cast<std::size_t>(-1);
  const std::string message = "Loaded mesh and cleared existing entities.";
  entity_runtime_.SetUiStatusMessage(message);
  if (status_message) {
    *status_message = message;
  }
  return true;
}

bool EditorUiRuntime::CreateEntityFromDraft(std::string* status_message) {
  std::string mesh_path = entity_draft_.mesh_path;
  const bool should_load_mesh =
    entity_draft_.load_mesh_before_create && !mesh_path.empty() && mesh_path != entity_runtime_.mesh_source_path();
  if (should_load_mesh) {
    if (!LoadMeshAndResetBindings(mesh_path, status_message)) {
      return false;
    }
  }

  const int preset_index = std::clamp(entity_draft_.preset_index, 0, 2);
  const std::string entity_name = entity_draft_.entity_name;

  if (preset_index == 0) {
    CreateEntityInfo info = CreateCameraEntityInfo(entity_runtime_.mesh());
    info.instance_name = entity_name.empty() ? "camera_instance" : entity_name;
    const auto handle = entity_runtime_.LoadEntity(std::move(info));
    if (handle == entity_interaction::EntityInteractionRuntime::kInvalidEntityHandle) {
      const std::string message = "Failed to create camera entity.";
      entity_runtime_.SetUiStatusMessage(message);
      if (status_message) {
        *status_message = message;
      }
      return false;
    }
    const std::string message = "Created camera entity slot " + std::to_string(handle);
    entity_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return true;
  }

  const PhysSolverKind solver_kind = entity_draft_.solver_kind_index == 1 ? PhysSolverKind::Gpu : PhysSolverKind::Cpu;
  if (solver_kind == PhysSolverKind::Gpu && std::string(entity_draft_.gpu_shader_path).empty()) {
    const std::string message = "GPU preset needs a shader path.";
    entity_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  const std::string algorithm_name =
    std::string(entity_draft_.algorithm_name).empty()
      ? (solver_kind == PhysSolverKind::Gpu ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName)
      : entity_draft_.algorithm_name;

  CreateEntityInfo info = CreatePhysicsEntityInfo(
    entity_runtime_.mesh(),
    solver_kind,
    algorithm_name,
    ResolveMountedAgentNameForPreset(preset_index),
    entity_draft_.gpu_shader_path);
  info.instance_name = entity_name.empty() ? (algorithm_name.empty() ? "physics_instance" : algorithm_name + "_instance") : entity_name;

  const auto handle = entity_runtime_.LoadEntity(std::move(info));
  if (handle == entity_interaction::EntityInteractionRuntime::kInvalidEntityHandle) {
    const std::string message = "Failed to create entity.";
    entity_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  const std::string message = "Created entity slot " + std::to_string(handle);
  entity_runtime_.SetUiStatusMessage(message);
  if (status_message) {
    *status_message = message;
  }
  return true;
}

void EditorUiRuntime::DrawLoadedEntityListUi() {
  const auto& entity_slots = entity_runtime_.entity_slots();
  ImGui::Text("Loaded entities: %zu", entity_slots.size());
  ImGui::Separator();

  if (ImGui::BeginListBox("##entity_list", ImVec2(-FLT_MIN, 220.0f))) {
    for (std::size_t index = 0; index < entity_slots.size(); ++index) {
      const auto& entity = entity_slots[index];
      if (!entity) {
        continue;
      }

      std::string label = "[" + std::to_string(index) + "] ";
      label += entity->instance_name().empty() ? entity->algorithm_name() : entity->instance_name();
      label += " | ";
      label += entity->mounted_agent_name();
      if (entity->mounted_agent_name() == "render_physics_agent" || entity->mounted_agent_name() == "dual_agent") {
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

  const bool has_selected = selected_entity_slot_ < entity_slots.size() && entity_slots[selected_entity_slot_];
  if (has_selected) {
    const auto& entity = entity_slots[selected_entity_slot_];
    ImGui::Text("Selected: %s", entity->instance_name().empty() ? entity->algorithm_name().c_str() : entity->instance_name().c_str());
    ImGui::Text("Agent: %s", entity->mounted_agent_name().c_str());
    if (entity->mounted_agent_name() == "render_physics_agent" || entity->mounted_agent_name() == "dual_agent") {
      ImGui::TextUnformatted("Role: shared render + physics");
    }
    ImGui::Text("Algorithm: %s", entity->algorithm_name().c_str());
    if (ImGui::Button("Unload Selected")) {
      entity_runtime_.UnloadEntity(selected_entity_slot_);
      selected_entity_slot_ = static_cast<std::size_t>(-1);
      entity_runtime_.SetUiStatusMessage("Unloaded selected entity.");
    }
  } else {
    ImGui::TextUnformatted("No entity selected.");
  }
}

void EditorUiRuntime::DrawEntityDraftUi() {
  static constexpr const char* kSolverLabels[] = {
    "CPU",
    "GPU",
  };

  ImGui::InputText("Entity Name", entity_draft_.entity_name, sizeof(entity_draft_.entity_name));
  ImGui::InputText("Mesh File", entity_draft_.mesh_path, sizeof(entity_draft_.mesh_path));
  ImGui::Checkbox("Load Mesh Before Create", &entity_draft_.load_mesh_before_create);

  if (ImGui::CollapsingHeader("Advanced Presets")) {
    ImGui::Combo("Solver", &entity_draft_.solver_kind_index, kSolverLabels, static_cast<int>(std::size(kSolverLabels)));
    if (entity_draft_.solver_kind_index == 1) {
      ImGui::InputText("GPU Shader", entity_draft_.gpu_shader_path, sizeof(entity_draft_.gpu_shader_path));
    }

    if (ImGui::Button("Create Camera Entity")) {
      entity_draft_.preset_index = 0;
      std::snprintf(entity_draft_.algorithm_name, sizeof(entity_draft_.algorithm_name), "%s", kCameraAlgorithmName);
      std::string message;
      if (!CreateEntityFromDraft(&message)) {
        entity_runtime_.SetUiStatusMessage(message.empty() ? "Failed to create camera entity." : message);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Physics Entity")) {
      entity_draft_.preset_index = entity_draft_.solver_kind_index == 1 ? 2 : 1;
      std::snprintf(
        entity_draft_.algorithm_name,
        sizeof(entity_draft_.algorithm_name),
        "%s",
        entity_draft_.solver_kind_index == 1 ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName);
      std::string message;
      if (!CreateEntityFromDraft(&message)) {
        entity_runtime_.SetUiStatusMessage(message.empty() ? "Failed to create physics entity." : message);
      }
    }
  }

  if (ImGui::Button("Load Mesh")) {
    std::string message;
    if (!LoadMeshAndResetBindings(entity_draft_.mesh_path, &message)) {
      entity_runtime_.SetUiStatusMessage(message.empty() ? "Failed to load mesh." : message);
    } else {
      entity_runtime_.SetUiStatusMessage(message);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Entity Form")) {
    ResetEntityDraftState();
    entity_runtime_.SetUiStatusMessage("Entity form reset.");
  }
}

void EditorUiRuntime::DrawEditorUi() {
  const auto& mesh = entity_runtime_.mesh();
  const auto& vertex_positions = entity_runtime_.active_vertex_positions();
  agents::DrawVertexArrayOverlay(vertex_positions, mesh.edges, mesh.triangles);

  ImGui::Begin("Editor UI");
  ImGui::Text("Mesh source: %s", entity_runtime_.mesh_source_path().empty() ? "<in-memory>" : entity_runtime_.mesh_source_path().c_str());
  ImGui::TextUnformatted("Rendering the algorithm vertex array, not the source mesh object.");
  ImGui::Text("Status: %s", entity_runtime_.ui_status_message().empty() ? "ready" : entity_runtime_.ui_status_message().c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Create Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawEntityDraftUi();
  }

  if (ImGui::CollapsingHeader("Loaded Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLoadedEntityListUi();
  }

  ImGui::Separator();
  ImGui::Text("Active render entity: %s", entity_runtime_.render_entity_ready() && entity_runtime_.render_entity()
    ? entity_runtime_.render_entity()->instance_name().c_str()
    : "<none>");
  ImGui::Text("Active physics entity: %s", entity_runtime_.physics_entity_ready() && entity_runtime_.physics_entity()
    ? entity_runtime_.physics_entity()->instance_name().c_str()
    : "<none>");
  if (entity_runtime_.render_entity_ready() && entity_runtime_.physics_entity_ready() &&
      entity_runtime_.render_entity() == entity_runtime_.physics_entity()) {
    ImGui::Text("Active shared entity: %s", entity_runtime_.render_entity()->instance_name().c_str());
  }
  ImGui::End();
}

bool EditorUiRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  if (!entity_runtime_.Init(mesh, window_title ? window_title : "Editor UI", width, height)) {
    return false;
  }
  entity_runtime_.SetDrawCallback([this]() { DrawEditorUi(); });
  ResetEntityDraftState();
  entity_runtime_.SetUiStatusMessage("Mesh is prefilled. Create the entity to start work.");
  return true;
}

bool EditorUiRuntime::LoadMeshFromFile(const std::string& path, std::string* error_message) {
  if (!entity_runtime_.LoadMeshFromFile(path, error_message)) {
    selected_entity_slot_ = static_cast<std::size_t>(-1);
    ResetEntityDraftState();
    return false;
  }
  selected_entity_slot_ = static_cast<std::size_t>(-1);
  ResetEntityDraftState();
  entity_runtime_.SetUiStatusMessage("Loaded mesh: " + path);
  return true;
}

bool EditorUiRuntime::Tick() {
  return entity_runtime_.Tick();
}

void EditorUiRuntime::Destroy() {
  entity_runtime_.Destroy();
  selected_entity_slot_ = static_cast<std::size_t>(-1);
  ResetEntityDraftState();
}

}  // namespace editor_ui
