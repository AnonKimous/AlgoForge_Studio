#pragma once

#include "common_data/mesh.h"
#include "service_domains/render/renderer.h"
#include "service_domains/render/scene_camera.h"
#include "common_data/viewport_transform.h"
#include "runtime_systems/window/window.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace agents {

struct SceneViewFrameState {
  Vec2 mouse_pixel{};
};

class WindowAgent {
 public:
  bool Init(const char* title, int width, int height);
  bool Tick();
  void Destroy();

  WindowHandle native_handle() const;
  int width() const;
  int height() const;
  const InputState& input() const;
  Vec2 MousePosition() const;

 private:
  std::unique_ptr<SdlWindow> window_;
};

class SceneViewAgent {
 public:
  void Init(const Mesh& mesh);
  SceneViewFrameState Tick(const SceneViewBounds& scene_bounds, const WindowAgent& window_agent);
  void Destroy();

  const SceneCamera& camera() const { return camera_; }
  const ViewportTransform& viewport() const { return viewport_; }

 private:
  SceneCamera camera_{};
  ViewportTransform viewport_{};
};

class RenderAgent {
 public:
  bool Init(const WindowHandle& window_handle);
  RenderFrameResult Tick(const Mesh& mesh, const SceneCamera& camera, const RenderUiState& ui_state);
  void Destroy();

  InteractionMode mode() const;
  PhysRunState phys_run_state() const;
  void SetPhysRunState(PhysRunState state);
  SceneViewBounds scene_view_bounds() const;
  VulkanComputeContextView compute_context() const;

 private:
  std::unique_ptr<VulkanRenderer> renderer_;
};

}  // namespace agents
