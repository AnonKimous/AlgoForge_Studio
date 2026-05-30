#pragma once

#include "messaging/io_bus.h"
#include "data_protocol/mesh.h"
#include "service_domains/render/renderer.h"
#include "service_domains/render/scene_camera.h"
#include "data_protocol/triangle_orientation_state.h"
#include "foundation/viewport_transform.h"
#include "runtime_systems/window/window.h"

#include <array>
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
  RenderFrameResult Tick(const Mesh& mesh, const TriangleOrientationState& orientation_state, const SceneCamera& camera, int highlighted_vertex, const RenderUiState& ui_state);
  void Destroy();

  InteractionMode mode() const;
  PhysRunState phys_run_state() const;
  bool phys_guide_enabled() const;
  void SetPhysRunState(PhysRunState state);
  void SetPhysGuideEnabled(bool enabled);
  SceneViewBounds scene_view_bounds() const;
  VulkanComputeContextView compute_context() const;

 private:
  std::unique_ptr<VulkanRenderer> renderer_;
};

class IoBusAgent {
 public:
  void Init();
  void Destroy();
  IoBufferEndpoint AllocateBuffer(const std::string& module_name, uint32_t buffer_id = 0, bool lock_required = false);
  std::array<IoBufferEndpoint, 2> AllocateFastChannel(const std::string& channel_name, bool lock_required = true);
  bool PublishToBuffer(const std::string& module_name, uint32_t buffer_id, const IoBufferPacket& packet);
  bool PublishToFastChannel(const std::string& channel_name, const IoBufferPacket& packet);

 private:
  messaging::SharedIoBus shared_bus_{};
};

}  // namespace agents
