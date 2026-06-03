#include "interact_ui_runtime.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"
#include "resource/mesh_resource.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <chrono>
#include <exception>
#include <imgui.h>
#include <iterator>
#include <utility>

namespace interact_ui {

void InteractUiRuntime::ResetAgentDraftState() {
  std::snprintf(agent_draft_.agent_name, sizeof(agent_draft_.agent_name), "%s", "agent_0");
  std::snprintf(agent_draft_.algorithm_name, sizeof(agent_draft_.algorithm_name), "%s", kCorotatedCpuAlgorithmName);
  std::snprintf(agent_draft_.mesh_path, sizeof(agent_draft_.mesh_path), "%s", mesh_source_path_.c_str());
  agent_draft_.gpu_shader_path[0] = '\0';
  agent_draft_.preset_index = 1;
  agent_draft_.solver_kind_index = 0;
  agent_draft_.load_mesh_before_create = true;
}

bool InteractUiRuntime::LoadMeshAndResetState(const std::string& path, std::string* status_message) {
  try {
    mesh_ = LoadMeshObjFile(path);
    mesh_source_path_ = path;
    execute_runtime_.UnloadAgent();
    ResetAgentDraftState();
    current_agent_id_ = 0;
    const std::string message = "Loaded mesh: " + path;
    ui_status_message_ = message;
    if (status_message) {
      *status_message = message;
    }
    return true;
  } catch (const std::exception& error) {
    const std::string message = std::string("Mesh load failed: ") + error.what();
    ui_status_message_ = message;
    if (status_message) {
      *status_message = message;
    }
    return false;
  }
}

bool InteractUiRuntime::CreateAgentFromDraft(std::string* status_message) {
  std::string mesh_path = agent_draft_.mesh_path;
  const bool should_load_mesh =
    agent_draft_.load_mesh_before_create && !mesh_path.empty() && mesh_path != mesh_source_path_;
  if (should_load_mesh) {
    if (!LoadMeshAndResetState(mesh_path, status_message)) {
      return false;
    }
  }

  const int preset_index = std::clamp(agent_draft_.preset_index, 0, 2);
  const std::string agent_name = agent_draft_.agent_name;

  agent_execute::AgentLaunchSpec spec{};
  if (preset_index == 0) {
    spec = CreateCameraAgentInfo(mesh_);
    spec.agent_config.agent_name = agent_name.empty() ? "camera_agent" : agent_name;
  } else {
    const PhysSolverKind solver_kind = agent_draft_.solver_kind_index == 1 ? PhysSolverKind::Gpu : PhysSolverKind::Cpu;
    if (solver_kind == PhysSolverKind::Gpu && std::string(agent_draft_.gpu_shader_path).empty()) {
      const std::string message = "GPU preset needs a shader path.";
      ui_status_message_ = message;
      if (status_message) {
        *status_message = message;
      }
      return false;
    }

    const std::string algorithm_name =
      std::string(agent_draft_.algorithm_name).empty()
        ? (solver_kind == PhysSolverKind::Gpu ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName)
        : agent_draft_.algorithm_name;

    spec = CreatePhysicsAgentInfo(
      mesh_,
      solver_kind,
      algorithm_name,
      agent_draft_.gpu_shader_path);
    spec.agent_config.agent_name = agent_name.empty() ? (algorithm_name.empty() ? "agent" : algorithm_name + "_agent") : agent_name;
  }

  if (!execute_runtime_.LoadAgent(std::move(spec))) {
    const std::string message = "Failed to create agent.";
    ui_status_message_ = message;
    if (status_message) {
      *status_message = message;
    }
    return false;
  }

  current_agent_id_ += 1;
  const std::string message = "Created agent #" + std::to_string(current_agent_id_);
  ui_status_message_ = message;
  if (status_message) {
    *status_message = message;
  }
  return true;
}

void InteractUiRuntime::DrawLoadedAgentUi() {
  ImGui::Text("Current agent: %s", execute_runtime_.has_agent() && execute_runtime_.current_agent()
    ? (execute_runtime_.current_agent()->agent_name().empty() ? execute_runtime_.current_agent()->algorithm_name().c_str() : execute_runtime_.current_agent()->agent_name().c_str())
    : "<none>");
  ImGui::Text("Algorithm: %s", execute_runtime_.has_agent() && execute_runtime_.current_agent()
    ? execute_runtime_.current_agent()->algorithm_name().c_str()
    : "<none>");
  if (execute_runtime_.has_agent()) {
    if (ImGui::Button("Unload Agent")) {
      execute_runtime_.UnloadAgent();
      ui_status_message_ = "Unloaded current agent.";
    }
  } else {
    ImGui::TextUnformatted("No agent loaded.");
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
        ui_status_message_ = message.empty() ? "Failed to create camera agent." : message;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Agent")) {
      agent_draft_.preset_index = agent_draft_.solver_kind_index == 1 ? 2 : 1;
      std::snprintf(
        agent_draft_.algorithm_name,
        sizeof(agent_draft_.algorithm_name),
        "%s",
        agent_draft_.solver_kind_index == 1 ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName);
      std::string message;
      if (!CreateAgentFromDraft(&message)) {
        ui_status_message_ = message.empty() ? "Failed to create agent." : message;
      }
    }
  }

  if (ImGui::Button("Load Mesh")) {
    std::string message;
    if (!LoadMeshAndResetState(agent_draft_.mesh_path, &message)) {
      ui_status_message_ = message.empty() ? "Failed to load mesh." : message;
    } else {
      ui_status_message_ = message;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Agent Form")) {
    ResetAgentDraftState();
    ui_status_message_ = "Agent form reset.";
  }
}

void InteractUiRuntime::DrawInteractUi() {
  execute_runtime_.Tick(mesh_, window_agent_.input(), window_agent_.MousePosition(), frame_dt_);
  agent_execute::DrawVertexArrayOverlay(mesh_.positions, mesh_.edges, mesh_.triangles);

  ImGui::Begin("Interact & UI");
  ImGui::Text("Mesh source: %s", mesh_source_path_.empty() ? "<in-memory>" : mesh_source_path_.c_str());
  ImGui::Text("Status: %s", ui_status_message_.empty() ? "ready" : ui_status_message_.c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Create Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAgentDraftUi();
  }

  if (ImGui::CollapsingHeader("Loaded Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLoadedAgentUi();
  }

  ImGui::Separator();
  ImGui::Text("Algorithm signal: %s", execute_runtime_.algorithm_to_agent_signal().pause_requested ? "pause requested" : "idle");
  ImGui::End();
}

bool InteractUiRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  mesh_source_path_.clear();
  if (!window_agent_.Init(window_title ? window_title : "Interact & UI", width, height)) {
    return false;
  }
  window_agent_.SetDrawCallback([this]() {
    DrawInteractUi();
  });
  ResetAgentDraftState();
  ui_status_message_ = "Mesh is prefilled. Create the agent to start work.";
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool InteractUiRuntime::LoadMeshFromFile(const std::string& path, std::string* error_message) {
  if (!LoadMeshAndResetState(path, error_message)) {
    return false;
  }
  ui_status_message_ = "Loaded mesh: " + path;
  return true;
}

bool InteractUiRuntime::Tick() {
  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;
  return window_agent_.Tick();
}

void InteractUiRuntime::Destroy() {
  execute_runtime_.Destroy();
  window_agent_.Destroy();
  mesh_ = Mesh{};
  mesh_source_path_.clear();
  ui_status_message_.clear();
  current_agent_id_ = 0;
  ResetAgentDraftState();
}

}  // namespace interact_ui
