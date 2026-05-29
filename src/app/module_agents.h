#pragma once

#include "../interaction/edit_mode_controller.h"
#include "../interaction/guide_ui_controller.h"
#include "../interaction/phys_mode_controller.h"
#include "../communication/io_bus.h"
#include "../mesh.h"
#include "../render/renderer.h"
#include "../render/scene_camera.h"
#include "../triangle_orientation_state.h"
#include "../validation_bridge/validation_bridge.h"
#include "../validation_layer/validation_actions.h"
#include "../validation_layer/validation_layer.h"
#include "../viewport_transform.h"
#include "../window/window.h"

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
  IoBufferEndpoint io_endpoint();
  void ResolveIncomingIoBuffers();

  InteractionMode mode() const;
  PhysRunState phys_run_state() const;
  bool phys_guide_enabled() const;
  void SetPhysRunState(PhysRunState state);
  void SetPhysGuideEnabled(bool enabled);
  SceneViewBounds scene_view_bounds() const;
  VulkanComputeContextView compute_context() const;

 private:
  std::unique_ptr<VulkanRenderer> renderer_;
  IoBufferPacket inbound_io_packet_{};
};

class ValidationAgent {
 public:
  bool Init(bool enabled);
  std::vector<ValidationAction> Tick();
  void PublishFrame(const ValidationFrameSnapshot& snapshot);
  void Destroy();
  IoBufferEndpoint io_endpoint();
  void ResolveIncomingIoBuffers();

  bool enabled() const { return enabled_; }

 private:
  bool enabled_{false};
  ValidationLayerApi validation_layer_{};
  IoBufferPacket inbound_io_packet_{};
};

class EditModeAgent {
 public:
  void Init();
  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel);
  void Destroy();

 private:
  std::unique_ptr<interaction_analysis::EditModeController> controller_;
};

class GuideUiAgent {
 public:
  void Init();
  GuideUiFrame Tick(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel);
  void Destroy();

 private:
  std::unique_ptr<interaction_analysis::GuideUiController> controller_;
};

class PhysAgent {
 public:
  void Init(const PhysManagerConfig& config, const VulkanComputeContextView& compute_context);
  void TickModeTransition(InteractionMode mode, Mesh& mesh, const std::filesystem::path& snapshot_path);
  void SetSelectedGuideVertices(const std::vector<int>& vertices);
  IoBufferEndpoint io_endpoint();
  void ResolveIncomingIoBuffers(Mesh& mesh);
  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  void SetRunState(PhysRunState run_state);
  void SetGuideEnabled(bool enabled);
  void SetGuideEditMode(GuideEditMode mode);
  void SetGuideVelocitySettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames);
  void SetGuideForceSettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames);
  PhysRunState run_state() const;
  bool guide_enabled() const;
  int selected_velocity_guidance() const;
  float guide_velocity_magnitude() const;
  uint32_t guide_velocity_delay_frames() const;
  uint32_t guide_velocity_duration_frames() const;
  float guide_force_magnitude() const;
  uint32_t guide_force_delay_frames() const;
  uint32_t guide_force_duration_frames() const;
  const std::vector<VelocityMatrix>& total_velocities() const;
  const std::vector<VelocityMatrix>& linear_velocities() const;
  const std::vector<VelocityMatrix>& angular_velocities() const;
  const std::vector<PhysRecordedFrame>& recorded_frames() const;
  const std::vector<PhysGuideKeyframe>& guide_keyframes() const;
  const std::vector<VelocityGuidance>& active_velocity_guidances() const;
  const std::vector<VelocityGuideVelocity>& active_guide_velocities() const;
  const std::vector<VelocityGuideForce>& active_guide_forces() const;
  const std::vector<int>& selected_guide_vertices() const;
  GuideEditMode guide_edit_mode() const;
  int current_frame_index() const;
  PhysSolverKind solver_kind() const;
  const std::string& algorithm_name() const;
  const GpuPhysicsDispatchDebugInfo& gpu_dispatch_debug_info() const;

  void StepOnce(Mesh& mesh);
  void Reset(Mesh& mesh);
  void CacheCurrentState();
  void RestoreRecordedFrame(Mesh& mesh, int state_index);
  void SetRecordedFrameExpanded(int state_index, bool expanded);
  void SetGuideKeyframeEnabled(int frame_index, bool enabled);
  void SetGuideKeyframeExpandedByIndex(int index, bool expanded);

 private:
  std::unique_ptr<interaction_analysis::PhysModeController> controller_;
  InteractionMode previous_mode_{InteractionMode::Edit};
  IoBufferPacket inbound_io_packet_{};
};

class IoBusAgent {
 public:
  void Init();
  void Destroy();
  void BindSharedEndpoint(const std::string& endpoint_name, IoBufferEndpoint endpoint);
  void BindGuideUiDirectLine(IoBufferEndpoint endpoint);
  bool PublishToSharedEndpoint(const std::string& endpoint_name, const IoBufferPacket& packet);
  bool PublishGuideUiDirectLine(const IoBufferPacket& packet);

 private:
  communication::SharedIoBus shared_bus_{};
  communication::DedicatedIoLine guide_ui_direct_line_{};
};

class TriangleAnalysisAgent {
 public:
  void Init(const Mesh& reference_mesh);
  void Tick(const Mesh& mesh);
  void Destroy();

  const TriangleOrientationState& state() const { return state_; }

 private:
 Mesh reference_mesh_{};
  TriangleOrientationState state_{};
  bool initialized_{false};
};
}  // namespace agents
