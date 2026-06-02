#include "interact_ui_runtime.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <imgui.h>
#include <iterator>
#include <utility>

namespace interact_ui {

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

void InteractUiRuntime::ResetAgentDraftState() {
  std::snprintf(agent_draft_.agent_name, sizeof(agent_draft_.agent_name), "%s", "agent_0");
  std::snprintf(agent_draft_.algorithm_name, sizeof(agent_draft_.algorithm_name), "%s", kCorotatedCpuAlgorithmName);
  std::snprintf(agent_draft_.mesh_path, sizeof(agent_draft_.mesh_path), "%s", execute_runtime_.mesh_source_path().c_str());
  agent_draft_.gpu_shader_path[0] = '\0';
  agent_draft_.preset_index = 1;
  agent_draft_.solver_kind_index = 0;
  agent_draft_.load_mesh_before_create = true;
}

bool InteractUiRuntime::LoadMeshAndResetBindings(const std::string& path, std::string* status_message) {
  std::string error_message;
  if (!execute_runtime_.LoadMeshFromFile(path, &error_message)) {
    const std::string message = error_message.empty() ? "Failed to load mesh." : error_message;
    execute_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  ResetAgentDraftState();
  selected_agent_slot_ = static_cast<std::size_t>(-1);
  const std::string message = "Loaded mesh and cleared existing agents.";
  execute_runtime_.SetUiStatusMessage(message);
  if (status_message) {
    *status_message = message;
  }
  return true;
}

bool InteractUiRuntime::CreateAgentFromDraft(std::string* status_message) {
  std::string mesh_path = agent_draft_.mesh_path;
  const bool should_load_mesh =
    agent_draft_.load_mesh_before_create && !mesh_path.empty() && mesh_path != execute_runtime_.mesh_source_path();
  if (should_load_mesh) {
    if (!LoadMeshAndResetBindings(mesh_path, status_message)) {
      return false;
    }
  }

  const int preset_index = std::clamp(agent_draft_.preset_index, 0, 2);
  const std::string agent_name = agent_draft_.agent_name;

  if (preset_index == 0) {
    CreateAgentInfo info = CreateCameraAgentInfo(execute_runtime_.mesh());
    info.agent_name = agent_name.empty() ? "camera_agent" : agent_name;
    const auto handle = execute_runtime_.LoadAgent(std::move(info));
    if (handle == agent_execute::AgentExecuteRuntime::kInvalidAgentHandle) {
      const std::string message = "Failed to create camera agent.";
      execute_runtime_.SetUiStatusMessage(message);
      if (status_message) {
        *status_message = message;
      }
      return false;
    }
    const std::string message = "Created camera agent slot " + std::to_string(handle);
    execute_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return true;
  }

  const PhysSolverKind solver_kind = agent_draft_.solver_kind_index == 1 ? PhysSolverKind::Gpu : PhysSolverKind::Cpu;
  if (solver_kind == PhysSolverKind::Gpu && std::string(agent_draft_.gpu_shader_path).empty()) {
    const std::string message = "GPU preset needs a shader path.";
    execute_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  const std::string algorithm_name =
    std::string(agent_draft_.algorithm_name).empty()
      ? (solver_kind == PhysSolverKind::Gpu ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName)
      : agent_draft_.algorithm_name;

  CreateAgentInfo info = CreatePhysicsAgentInfo(
    execute_runtime_.mesh(),
    solver_kind,
    algorithm_name,
    ResolveMountedAgentNameForPreset(preset_index),
    agent_draft_.gpu_shader_path);
  info.agent_name = agent_name.empty() ? (algorithm_name.empty() ? "physics_agent" : algorithm_name + "_agent") : agent_name;

  const auto handle = execute_runtime_.LoadAgent(std::move(info));
  if (handle == agent_execute::AgentExecuteRuntime::kInvalidAgentHandle) {
    const std::string message = "Failed to create agent.";
    execute_runtime_.SetUiStatusMessage(message);
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  const std::string message = "Created agent slot " + std::to_string(handle);
  execute_runtime_.SetUiStatusMessage(message);
  if (status_message) {
    *status_message = message;
  }
  return true;
}

void InteractUiRuntime::DrawLoadedAgentListUi() {
  const auto& agent_slots = execute_runtime_.agent_slots();
  ImGui::Text("Loaded agents: %zu", agent_slots.size());
  ImGui::Separator();

  if (ImGui::BeginListBox("##agent_list", ImVec2(-FLT_MIN, 220.0f))) {
    for (std::size_t index = 0; index < agent_slots.size(); ++index) {
      const auto& agent = agent_slots[index];
      if (!agent) {
        continue;
      }

      std::string label = "[" + std::to_string(index) + "] ";
      label += agent->agent_name().empty() ? agent->algorithm_name() : agent->agent_name();
      label += " | ";
      label += agent->mounted_agent_name();
      if (agent->mounted_agent_name() == "render_physics_agent" || agent->mounted_agent_name() == "dual_agent") {
        label += " [shared]";
      }
      label += " | ";
      label += agent->algorithm_name();

      const bool selected = (selected_agent_slot_ == index);
      if (ImGui::Selectable(label.c_str(), selected)) {
        selected_agent_slot_ = index;
      }
    }
    ImGui::EndListBox();
  }

  const bool has_selected = selected_agent_slot_ < agent_slots.size() && agent_slots[selected_agent_slot_];
  if (has_selected) {
    const auto& agent = agent_slots[selected_agent_slot_];
    ImGui::Text("Selected: %s", agent->agent_name().empty() ? agent->algorithm_name().c_str() : agent->agent_name().c_str());
    ImGui::Text("Mounted role: %s", agent->mounted_agent_name().c_str());
    if (agent->mounted_agent_name() == "render_physics_agent" || agent->mounted_agent_name() == "dual_agent") {
      ImGui::TextUnformatted("Role: shared render + physics");
    }
    ImGui::Text("Algorithm: %s", agent->algorithm_name().c_str());
    if (ImGui::Button("Unload Selected")) {
      execute_runtime_.UnloadAgent(selected_agent_slot_);
      selected_agent_slot_ = static_cast<std::size_t>(-1);
      execute_runtime_.SetUiStatusMessage("Unloaded selected agent.");
    }
  } else {
    ImGui::TextUnformatted("No agent selected.");
  }
}

void InteractUiRuntime::DrawAgentDraftUi() {
  static constexpr const char* kSolverLabels[] = {
    "CPU",
    "GPU",
  };

  ImGui::InputText("Agent Name", agent_draft_.agent_name, sizeof(agent_draft_.agent_name));
  ImGui::InputText("Mesh File", agent_draft_.mesh_path, sizeof(agent_draft_.mesh_path));
  ImGui::Checkbox("Load Mesh Before Create", &agent_draft_.load_mesh_before_create);

  if (ImGui::CollapsingHeader("Advanced Presets")) {
    ImGui::Combo("Solver", &agent_draft_.solver_kind_index, kSolverLabels, static_cast<int>(std::size(kSolverLabels)));
    if (agent_draft_.solver_kind_index == 1) {
      ImGui::InputText("GPU Shader", agent_draft_.gpu_shader_path, sizeof(agent_draft_.gpu_shader_path));
    }

    if (ImGui::Button("Create Camera Agent")) {
      agent_draft_.preset_index = 0;
      std::snprintf(agent_draft_.algorithm_name, sizeof(agent_draft_.algorithm_name), "%s", kCameraAlgorithmName);
      std::string message;
      if (!CreateAgentFromDraft(&message)) {
        execute_runtime_.SetUiStatusMessage(message.empty() ? "Failed to create camera agent." : message);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Physics Agent")) {
      agent_draft_.preset_index = agent_draft_.solver_kind_index == 1 ? 2 : 1;
      std::snprintf(
        agent_draft_.algorithm_name,
        sizeof(agent_draft_.algorithm_name),
        "%s",
        agent_draft_.solver_kind_index == 1 ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName);
      std::string message;
      if (!CreateAgentFromDraft(&message)) {
        execute_runtime_.SetUiStatusMessage(message.empty() ? "Failed to create physics agent." : message);
      }
    }
  }

  if (ImGui::Button("Load Mesh")) {
    std::string message;
    if (!LoadMeshAndResetBindings(agent_draft_.mesh_path, &message)) {
      execute_runtime_.SetUiStatusMessage(message.empty() ? "Failed to load mesh." : message);
    } else {
      execute_runtime_.SetUiStatusMessage(message);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Agent Form")) {
    ResetAgentDraftState();
    execute_runtime_.SetUiStatusMessage("Agent form reset.");
  }
}

void InteractUiRuntime::DrawInteractUi() {
  const auto& mesh = execute_runtime_.mesh();
  const auto& vertex_positions = execute_runtime_.active_vertex_positions();
  agent_execute::DrawVertexArrayOverlay(vertex_positions, mesh.edges, mesh.triangles);

  ImGui::Begin("Interact & UI");
  ImGui::Text("Mesh source: %s", execute_runtime_.mesh_source_path().empty() ? "<in-memory>" : execute_runtime_.mesh_source_path().c_str());
  ImGui::TextUnformatted("Rendering the agent-execute vertex array, not the source mesh object.");
  ImGui::Text("Status: %s", execute_runtime_.ui_status_message().empty() ? "ready" : execute_runtime_.ui_status_message().c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Create Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAgentDraftUi();
  }

  if (ImGui::CollapsingHeader("Loaded Agents", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLoadedAgentListUi();
  }

  ImGui::Separator();
  ImGui::Text("Active render agent: %s", execute_runtime_.render_agent_binding_ready() && execute_runtime_.render_agent_binding()
    ? execute_runtime_.render_agent_binding()->agent_name().c_str()
    : "<none>");
  ImGui::Text("Active physics agent: %s", execute_runtime_.physics_agent_binding_ready() && execute_runtime_.physics_agent_binding()
    ? execute_runtime_.physics_agent_binding()->agent_name().c_str()
    : "<none>");
  if (execute_runtime_.render_agent_binding_ready() && execute_runtime_.physics_agent_binding_ready() &&
      execute_runtime_.render_agent_binding() == execute_runtime_.physics_agent_binding()) {
    ImGui::Text("Active shared agent: %s", execute_runtime_.render_agent_binding()->agent_name().c_str());
  }
  ImGui::End();
}

bool InteractUiRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  if (!execute_runtime_.Init(mesh, window_title ? window_title : "Interact & UI", width, height)) {
    return false;
  }
  execute_runtime_.SetDrawCallback([this]() { DrawInteractUi(); });
  ResetAgentDraftState();
  execute_runtime_.SetUiStatusMessage("Mesh is prefilled. Create the agent to start work.");
  return true;
}

bool InteractUiRuntime::LoadMeshFromFile(const std::string& path, std::string* error_message) {
  if (!execute_runtime_.LoadMeshFromFile(path, error_message)) {
    selected_agent_slot_ = static_cast<std::size_t>(-1);
    ResetAgentDraftState();
    return false;
  }
  selected_agent_slot_ = static_cast<std::size_t>(-1);
  ResetAgentDraftState();
  execute_runtime_.SetUiStatusMessage("Loaded mesh: " + path);
  return true;
}

bool InteractUiRuntime::Tick() {
  return execute_runtime_.Tick();
}

void InteractUiRuntime::Destroy() {
  execute_runtime_.Destroy();
  selected_agent_slot_ = static_cast<std::size_t>(-1);
  ResetAgentDraftState();
}

}  // namespace interact_ui
