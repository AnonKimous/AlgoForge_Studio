#include "interact_ui_runtime.h"

#include <chrono>
#include <imgui.h>
#include <utility>

namespace interact_ui {

void InteractUiRuntime::SyncCustomInterventionUiState() {
  const std::shared_ptr<agent::Agent>& loaded_agent = execute_runtime_.loaded_agent();
  std::shared_ptr<agent::IAlgorithmInterventionPackageUi> hook{};
  if (loaded_agent && loaded_agent->intervention_package()) {
    hook = loaded_agent->intervention_package()->ui_hook;
  }

  if (hook == active_custom_ui_hook_) {
    return;
  }

  active_custom_ui_hook_ = std::move(hook);
  active_custom_ui_state_.reset();
  if (active_custom_ui_hook_) {
    active_custom_ui_state_ = active_custom_ui_hook_->CreateUiState();
  }
}

void InteractUiRuntime::DrawCustomInterventionUi() {
  SyncCustomInterventionUiState();

  const std::shared_ptr<agent::Agent>& loaded_agent = execute_runtime_.loaded_agent();
  if (!loaded_agent || !loaded_agent->intervention_package() || !active_custom_ui_hook_) {
    ImGui::TextUnformatted("No custom intervention UI.");
    return;
  }
  if (!active_custom_ui_state_) {
    ImGui::TextUnformatted("Custom intervention UI state is unavailable.");
    return;
  }

  const agent::AgentInterventionUiContext context{
    .agent = loaded_agent.get(),
    .mesh = &mesh_,
    .input = &runtime_environment_.input(),
    .mouse_pixel = runtime_environment_.MousePosition(),
    .dt_seconds = frame_dt_,
    .algorithm_to_agent_signal = &execute_runtime_.algorithm_to_agent_signal(),
  };
  active_custom_ui_hook_->DrawUi(context, active_custom_ui_state_.get());
}

void InteractUiRuntime::DrawAgentBindingUi() {
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

  if (execute_runtime_.has_agent() && execute_runtime_.loaded_agent() &&
      execute_runtime_.loaded_agent()->intervention_package() &&
      execute_runtime_.loaded_agent()->intervention_package()->ui_hook) {
    ImGui::Spacing();
    const char* custom_title = execute_runtime_.loaded_agent()->intervention_package()->ui_hook->title();
    ImGui::SeparatorText(custom_title ? custom_title : "Custom Intervention UI");
    DrawCustomInterventionUi();
  }
}

void InteractUiRuntime::DrawInteractUi() {
  const InputState& input = runtime_environment_.input();
  const Vec2 mouse_position = runtime_environment_.MousePosition();
  execute_runtime_.Tick(mesh_, input, mouse_position, frame_dt_);

  ImGui::Begin("Interact & UI");
  ImGui::Text("Status: %s", ui_status_message_.empty() ? "ready" : ui_status_message_.c_str());
  ImGui::Text("Mesh: %zu vertices, %zu triangles", mesh_.positions.size(), mesh_.triangles.size());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Agent Binding", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAgentBindingUi();
  }

  ImGui::Separator();
  ImGui::Text("Algorithm signal: %s", execute_runtime_.algorithm_to_agent_signal().pause_requested ? "pause requested" : "idle");
  ImGui::End();
}

bool InteractUiRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  if (!runtime_environment_.Init(window_title ? window_title : "Interact & UI", width, height)) {
    return false;
  }
  runtime_environment_.SetDrawCallback([this]() {
    DrawInteractUi();
  });
  ui_status_message_ = mesh_.positions.empty() ? "Mesh is empty." : "Mesh is preloaded.";
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool InteractUiRuntime::Tick() {
  if (!runtime_environment_.has_window()) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;
  return runtime_environment_.Tick();
}

void InteractUiRuntime::Destroy() {
  execute_runtime_.Destroy();
  runtime_environment_.Destroy();
  mesh_ = Mesh{};
  ui_status_message_.clear();
  active_custom_ui_hook_.reset();
  active_custom_ui_state_.reset();
}

}  // namespace interact_ui
