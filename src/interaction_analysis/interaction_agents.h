#pragma once

#include "edit_mode_controller.h"
#include "guide_ui_controller.h"
#include "phys_mode_controller.h"

#include "data_protocol/interaction/interaction_state.h"
#include "data_protocol/mesh.h"
#include "data_protocol/triangle_orientation_state.h"
#include "decomposition/decomposition_manager.h"
#include "messaging/io_buffers.h"
#include "service_domains/physics/physics_module.h"
#include "service_domains/render/scene_camera.h"
#include "foundation/viewport_transform.h"
#include "foundation/input_state.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace interaction_analysis {

class EditModeAgent {
 public:
  void Init();
  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel);
  void Destroy();

 private:
  std::unique_ptr<EditModeController> controller_;
};

class GuideUiAgent {
 public:
  void Init();
  GuideUiFrame Tick(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel);
  void Destroy();

 private:
  std::unique_ptr<GuideUiController> controller_;
};

class PhysAgent {
 public:
  void Init(
    const PhysSolverConfig& config,
    const VulkanComputeContextView& compute_context,
    const AlgorithmComplianceDescriptor& compliance_descriptor = {},
    IoBufferEndpoint guide_ui_io_endpoint = {});
  void TickModeTransition(InteractionMode mode, Mesh& mesh, const std::filesystem::path& snapshot_path);
  void SetSelectedGuideVertices(const std::vector<int>& vertices);
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
  std::unique_ptr<PhysModeController> controller_;
  InteractionMode previous_mode_{InteractionMode::Edit};
  IoBufferEndpoint guide_ui_io_endpoint_{};
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

}  // namespace interaction_analysis
