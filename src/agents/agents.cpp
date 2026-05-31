#include "agents.h"

namespace agents {

bool WindowAgent::Init(const char* title, int width, int height) {
  window_ = std::make_unique<SdlWindow>(title, width, height);
  return true;
}

bool WindowAgent::Tick() {
  return window_ && window_->ProcessEvents();
}

void WindowAgent::Destroy() {
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

void SceneViewAgent::Init(const Mesh& mesh) {
  camera_.FitToMesh(mesh);
  viewport_ = ViewportTransform{};
}

SceneViewFrameState SceneViewAgent::Tick(const SceneViewBounds& scene_bounds, const WindowAgent& window_agent) {
  SceneViewFrameState frame{};
  frame.mouse_pixel = window_agent.MousePosition();
  if (scene_bounds.valid) {
    frame.mouse_pixel.x -= scene_bounds.x;
    frame.mouse_pixel.y -= scene_bounds.y;
    viewport_.SetSize(static_cast<int>(scene_bounds.width), static_cast<int>(scene_bounds.height));
    camera_.SetViewportSize(static_cast<int>(scene_bounds.width), static_cast<int>(scene_bounds.height));
  } else {
    viewport_.SetSize(window_agent.width(), window_agent.height());
  }
  return frame;
}

void SceneViewAgent::Destroy() {
  camera_ = SceneCamera{};
  viewport_ = ViewportTransform{};
}

bool RenderAgent::Init(const WindowHandle& window_handle) {
  renderer_ = std::make_unique<VulkanRenderer>(window_handle);
  return true;
}

RenderFrameResult RenderAgent::Tick(const Mesh& mesh, const SceneCamera& camera, const RenderUiState& ui_state) {
  return renderer_->Draw(mesh, camera, ui_state);
}

void RenderAgent::Destroy() {
  renderer_.reset();
}

InteractionMode RenderAgent::mode() const {
  return renderer_ ? renderer_->mode() : InteractionMode::Edit;
}

PhysRunState RenderAgent::phys_run_state() const {
  return renderer_ ? renderer_->phys_run_state() : PhysRunState::Pause;
}

void RenderAgent::SetPhysRunState(PhysRunState state) {
  if (renderer_) renderer_->SetPhysRunState(state);
}

SceneViewBounds RenderAgent::scene_view_bounds() const {
  return renderer_ ? renderer_->scene_view_bounds() : SceneViewBounds{};
}

VulkanComputeContextView RenderAgent::compute_context() const {
  return renderer_ ? renderer_->compute_context() : VulkanComputeContextView{};
}

}  // namespace agents
