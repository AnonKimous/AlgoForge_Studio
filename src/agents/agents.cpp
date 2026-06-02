#include "agents.h"

#include "codec/codec_manager.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace agent_execute {

namespace {

ImVec2 ProjectMeshPoint(const Vec3& point, Vec2 min_bounds, Vec2 max_bounds, ImVec2 canvas_origin, ImVec2 canvas_size) {
  const float width = std::max(1e-3f, max_bounds.x - min_bounds.x);
  const float height = std::max(1e-3f, max_bounds.y - min_bounds.y);
  const float scale = std::min(canvas_size.x / width, canvas_size.y / height) * 0.72f;
  const float center_x = (min_bounds.x + max_bounds.x) * 0.5f;
  const float center_y = (min_bounds.y + max_bounds.y) * 0.5f;
  return ImVec2{
    canvas_origin.x + canvas_size.x * 0.5f + (point.x - center_x) * scale,
    canvas_origin.y + canvas_size.y * 0.5f - (point.y - center_y) * scale,
  };
}

void DrawVertexArrayOverlayImpl(
  const std::vector<Vec3>& vertex_positions,
  const std::vector<std::array<uint32_t, 2>>& triangle_edges,
  const std::vector<std::array<uint32_t, 3>>& triangles) {
  if (vertex_positions.empty()) {
    return;
  }

  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
  if (!draw_list) {
    return;
  }

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (!viewport) {
    return;
  }

  ImVec2 canvas_origin = viewport->WorkPos;
  ImVec2 canvas_size = viewport->WorkSize;
  if (canvas_size.x <= 4.0f || canvas_size.y <= 4.0f) {
    return;
  }

  constexpr float kOverlayPadding = 72.0f;
  constexpr float kVertexRadius = 5.5f;
  constexpr float kEdgeThickness = 2.5f;
  canvas_origin.x += kOverlayPadding;
  canvas_origin.y += kOverlayPadding;
  canvas_size.x = std::max(1.0f, canvas_size.x - kOverlayPadding * 2.0f);
  canvas_size.y = std::max(1.0f, canvas_size.y - kOverlayPadding * 2.0f);

  Vec2 min_bounds{vertex_positions.front().x, vertex_positions.front().y};
  Vec2 max_bounds = min_bounds;
  for (const Vec3& position : vertex_positions) {
    min_bounds.x = std::min(min_bounds.x, position.x);
    min_bounds.y = std::min(min_bounds.y, position.y);
    max_bounds.x = std::max(max_bounds.x, position.x);
    max_bounds.y = std::max(max_bounds.y, position.y);
  }

  draw_list->AddRect(
    canvas_origin,
    ImVec2{canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y},
    IM_COL32(40, 40, 50, 160),
    0.0f,
    0,
    1.0f);

  auto project = [&](uint32_t index) -> ImVec2 {
    return ProjectMeshPoint(vertex_positions[index], min_bounds, max_bounds, canvas_origin, canvas_size);
  };

  for (const auto& triangle : triangles) {
    const ImVec2 a = project(triangle[0]);
    const ImVec2 b = project(triangle[1]);
    const ImVec2 c = project(triangle[2]);
    draw_list->AddTriangle(a, b, c, IM_COL32(180, 180, 220, 120), 1.5f);
  }

  if (!triangle_edges.empty()) {
    for (const auto& edge : triangle_edges) {
      const ImVec2 a = project(edge[0]);
      const ImVec2 b = project(edge[1]);
      draw_list->AddLine(a, b, IM_COL32(180, 220, 255, 200), kEdgeThickness);
    }
  }

  for (size_t index = 0; index < vertex_positions.size(); ++index) {
    const ImVec2 p = project(static_cast<uint32_t>(index));
    draw_list->AddCircleFilled(p, kVertexRadius, IM_COL32(255, 120, 80, 255));
  }
}

}  // namespace

void DrawVertexArrayOverlay(
  const std::vector<Vec3>& vertex_positions,
  const std::vector<std::array<uint32_t, 2>>& triangle_edges,
  const std::vector<std::array<uint32_t, 3>>& triangles) {
  DrawVertexArrayOverlayImpl(vertex_positions, triangle_edges, triangles);
}

void AgentAlgorithmRuntime::Init(
  const PhysSolverConfig& config,
  const VulkanComputeContextView& compute_context,
  const AlgorithmContainerDescriptor& container_descriptor) {
  pool_.Init(config, compute_context, container_descriptor);
}

bool AgentAlgorithmRuntime::Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const {
  return pool_.Run(request, result);
}

void AgentAlgorithmRuntime::SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package) {
  pool_.SetInterventionPackage(std::move(package));
}

void AgentAlgorithmRuntime::ApplyInterventionRequest(const InteractionInterventionRequest& request) {
  intervention_request_ = request;
  algorithm_to_agent_signal_.intervention_applied = false;
  algorithm_to_agent_signal_.intervention_needed = request.enabled;

  CodecManager codec{};
  IoBufferPacket packet = codec.BuildAlgorithmInterventionPacket(request);
  InteractionInterventionRequest decoded{};
  if (codec.DecodeAlgorithmInterventionPacket(packet, &decoded)) {
    decoded.enabled = request.enabled;
    intervention_request_ = decoded;
    algorithm_to_agent_signal_.intervention_applied = request.enabled;
  }
}

bool WindowAgent::Init(const char* title, int width, int height) {
  window_ = std::make_unique<SdlWindow>(title, width, height);
  imgui_runtime_ = std::make_unique<runtime_systems::ImGuiVulkanRuntime>();
  if (!imgui_runtime_->Init(window_->native_handle().window, title ? title : "Agent Debug UI")) {
    imgui_runtime_.reset();
    window_.reset();
    return false;
  }
  return true;
}

bool WindowAgent::Tick() {
  if (!window_ || !window_->ProcessEvents()) {
    return false;
  }
  return !imgui_runtime_ || imgui_runtime_->Tick(window_->native_handle().window);
}

void WindowAgent::SetDrawCallback(DrawCallback callback) {
  if (imgui_runtime_) {
    imgui_runtime_->SetDrawCallback(std::move(callback));
  }
}

void WindowAgent::Destroy() {
  if (imgui_runtime_) {
    imgui_runtime_->Destroy();
    imgui_runtime_.reset();
  }
  window_.reset();
}

WindowHandle WindowAgent::native_handle() const {
  return window_ ? window_->native_handle() : WindowHandle{};
}

int WindowAgent::width() const {
  return window_ ? window_->width() : 1;
}

int WindowAgent::height() const {
  return window_ ? window_->height() : 1;
}

const InputState& WindowAgent::input() const {
  static const InputState kEmptyInput{};
  return window_ ? window_->input() : kEmptyInput;
}

Vec2 WindowAgent::MousePosition() const {
  return window_ ? window_->MousePosition() : Vec2{};
}

bool RenderAgent::Init(const WindowHandle& window_handle) {
  (void)window_handle;
  return true;
}

InteractionUiAction RenderAgent::Tick(const Mesh& mesh, const InteractionUiState& ui_state) {
  InteractionUiAction action{};
  action.draw_calls = static_cast<uint32_t>(mesh.positions.size() + mesh.edges.size() + mesh.triangles.size());
  action.mode = mode_;
  action.phys_run_state = phys_run_state_;
  action.agent_to_algorithm_signal = ui_state.agent_to_algorithm_signal;
  action.intervention_request = ui_state.intervention_request;
  return action;
}

void RenderAgent::Destroy() {
  mode_ = InteractionMode::Edit;
  phys_run_state_ = PhysRunState::Pause;
}

InteractionMode RenderAgent::mode() const {
  return mode_;
}

PhysRunState RenderAgent::phys_run_state() const {
  return phys_run_state_;
}

void RenderAgent::SetPhysRunState(PhysRunState state) {
  phys_run_state_ = state;
}

}  // namespace agent_execute
