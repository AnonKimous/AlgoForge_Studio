#include "interact_ui_runtime.h"

#include "sidecar/mesh_io.h"

#include <chrono>
#include <cstring>
#include <exception>
#include <imgui.h>

namespace interact_ui {

bool InteractUiRuntime::LoadMeshAndResetState(const std::string& path, std::string* status_message) {
  try {
    mesh_ = LoadMeshObjFile(path);
    mesh_source_path_ = path;
    execute_runtime_.UnloadAgent();
    if (mesh_source_path_.size() < sizeof(mesh_path_input_)) {
      std::memcpy(mesh_path_input_, mesh_source_path_.c_str(), mesh_source_path_.size() + 1u);
    } else {
      mesh_path_input_[0] = '\0';
    }
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

void InteractUiRuntime::DrawLoadedAgentUi() {
  ImGui::Text("Bound agent: %s", execute_runtime_.has_agent() && execute_runtime_.loaded_agent()
    ? (execute_runtime_.loaded_agent()->agent_name().empty() ? execute_runtime_.loaded_agent()->algorithm_name().c_str() : execute_runtime_.loaded_agent()->agent_name().c_str())
    : "<none>");
  ImGui::Text("Algorithm: %s", execute_runtime_.has_agent() && execute_runtime_.loaded_agent()
    ? execute_runtime_.loaded_agent()->algorithm_name().c_str()
    : "<none>");
  if (execute_runtime_.has_agent()) {
    if (ImGui::Button("Clear Agent Binding")) {
      execute_runtime_.UnloadAgent();
      ui_status_message_ = "Cleared agent binding.";
    }
  } else {
    ImGui::TextUnformatted("No agent bound.");
  }
}

void InteractUiRuntime::DrawMeshUi() {
  ImGui::InputText("Mesh File", mesh_path_input_, sizeof(mesh_path_input_));
  if (ImGui::Button("Load Mesh")) {
    std::string message;
    if (!LoadMeshAndResetState(mesh_path_input_, &message)) {
      ui_status_message_ = message.empty() ? "Failed to load mesh." : message;
    } else {
      ui_status_message_ = message;
    }
  }
}

void InteractUiRuntime::DrawInteractUi() {
  static const InputState kEmptyInput{};
  const InputState& input = window_ ? window_->input() : kEmptyInput;
  const Vec2 mouse_position = window_ ? window_->MousePosition() : Vec2{};
  execute_runtime_.Tick(mesh_, input, mouse_position, frame_dt_);

  ImGui::Begin("Interact & UI");
  ImGui::Text("Mesh source: %s", mesh_source_path_.empty() ? "<in-memory>" : mesh_source_path_.c_str());
  ImGui::Text("Status: %s", ui_status_message_.empty() ? "ready" : ui_status_message_.c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawMeshUi();
  }

  if (ImGui::CollapsingHeader("Agent Binding", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLoadedAgentUi();
  }

  ImGui::Separator();
  ImGui::Text("Algorithm signal: %s", execute_runtime_.algorithm_to_agent_signal().pause_requested ? "pause requested" : "idle");
  ImGui::End();
}

bool InteractUiRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  mesh_source_path_.clear();
  window_ = std::make_unique<SdlWindow>(window_title ? window_title : "Interact & UI", width, height);
  imgui_runtime_ = std::make_unique<runtime_systems::ImGuiVulkanRuntime>();
  if (!imgui_runtime_->Init(window_->native_handle().window, window_title ? window_title : "Interact & UI")) {
    imgui_runtime_.reset();
    window_.reset();
    return false;
  }
  imgui_runtime_->SetDrawCallback([this]() {
    DrawInteractUi();
  });
  if (mesh_source_path_.size() < sizeof(mesh_path_input_)) {
    std::memcpy(mesh_path_input_, mesh_source_path_.c_str(), mesh_source_path_.size() + 1u);
  } else {
    mesh_path_input_[0] = '\0';
  }
  ui_status_message_ = "Mesh is prefilled.";
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
  if (!window_ || !imgui_runtime_) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;
  if (!window_->ProcessEvents()) {
    return false;
  }
  return imgui_runtime_->Tick(window_->native_handle().window);
}

void InteractUiRuntime::Destroy() {
  execute_runtime_.Destroy();
  if (imgui_runtime_) {
    imgui_runtime_->Destroy();
    imgui_runtime_.reset();
  }
  window_.reset();
  mesh_ = Mesh{};
  mesh_source_path_.clear();
  mesh_path_input_[0] = '\0';
  ui_status_message_.clear();
}

}  // namespace interact_ui
