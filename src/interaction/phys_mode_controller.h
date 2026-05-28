#pragma once

#include "interaction_state.h"
#include "guide_ui_controller.h"
#include "../mesh.h"
#include "../physics/physics_module.h"
#include "../viewport_transform.h"
#include "window/input_state.h"

#include <cstdint>
#include <vector>

class PhysModeController {
 public:
  void PhysInit(const Mesh& mesh);
  void SetRunState(PhysRunState run_state);
  void SetGuideEnabled(bool enabled) { guide_enabled_ = enabled; }
  void SetGuideEditMode(GuideEditMode mode) { guide_edit_mode_ = mode; }
  void SetGuideVelocityMagnitude(float magnitude);
  void SetGuideVelocitySettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames);
  void SetGuideForceSettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames);
  void CacheCurrentState();
  void RestoreRecordedFrame(Mesh& mesh, int state_index);
  void SetRecordedFrameExpanded(int state_index, bool expanded);
  void SetGuideKeyframeEnabled(int frame_index, bool enabled);
  void SetGuideKeyframeExpanded(int frame_index, bool expanded);
  void SetGuideKeyframeExpandedByIndex(int index, bool expanded);
  void UpsertVelocityGuidanceAtCurrentFrame(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target);
  void UpsertVelocityGuidanceAtCurrentFrame(const Mesh& mesh, const std::vector<int>& vertices, Vec3 start, Vec3 requested_target);
  void StepOnce(Mesh& mesh);
  void Reset(Mesh& mesh);
  PhysRunState run_state() const { return run_state_; }
  bool guide_enabled() const { return guide_enabled_; }
  int selected_velocity_guidance() const { return selected_velocity_guidance_; }
  float guide_velocity_magnitude() const { return guide_velocity_magnitude_; }
  uint32_t guide_velocity_delay_frames() const { return guide_velocity_delay_frames_; }
  uint32_t guide_velocity_duration_frames() const { return guide_velocity_duration_frames_; }
  float guide_force_magnitude() const { return guide_force_magnitude_; }
  uint32_t guide_force_delay_frames() const { return guide_force_delay_frames_; }
  uint32_t guide_force_duration_frames() const { return guide_force_duration_frames_; }
  const std::vector<VelocityMatrix>& total_velocities() const { return total_velocities_; }
  const std::vector<VelocityMatrix>& linear_velocities() const { return linear_velocities_; }
  const std::vector<VelocityMatrix>& angular_velocities() const { return angular_velocities_; }
  const std::vector<PhysRecordedFrame>& recorded_frames() const { return recorded_frames_; }
  const std::vector<PhysGuideKeyframe>& guide_keyframes() const { return guide_keyframes_; }
  const std::vector<VelocityGuidance>& active_velocity_guidances() const { return solver_.guidances(); }
  const std::vector<VelocityGuideVelocity>& active_guide_velocities() const { return solver_.guide_velocities(); }
  const std::vector<VelocityGuideForce>& active_guide_forces() const { return solver_.guide_forces(); }
  const std::vector<int>& selected_guide_vertices() const { return selected_guide_vertices_; }
  GuideEditMode guide_edit_mode() const { return guide_edit_mode_; }
  int current_frame_index() const { return static_cast<int>(frame_index_); }

  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel, float dt_seconds);

 private:
  bool initialized_{false};
  PhysRunState run_state_{PhysRunState::Pause};
  bool guide_enabled_{true};
  GuideEditMode guide_edit_mode_{GuideEditMode::Displacement};
  int selected_velocity_guidance_{-1};
  float physics_accumulator_{0.0f};
  uint64_t frame_index_{0};
  std::vector<VelocityMatrix> total_velocities_;
  std::vector<VelocityMatrix> linear_velocities_;
  std::vector<VelocityMatrix> angular_velocities_;
  std::vector<PhysRecordedFrame> recorded_frames_;
  std::vector<PhysGuideKeyframe> guide_keyframes_;
  std::vector<int> selected_guide_vertices_;
  float guide_velocity_magnitude_{1.0f};
  uint32_t guide_velocity_delay_frames_{0};
  uint32_t guide_velocity_duration_frames_{1};
  float guide_force_magnitude_{1.0f};
  uint32_t guide_force_delay_frames_{0};
  uint32_t guide_force_duration_frames_{1};
  std::vector<Vec3> initial_positions_;
  PhysicsSolver solver_;
  GuideUiController guide_ui_;

  void ResetSimulation(Mesh& mesh);
  void RefreshCurrentRecordedFrame(const Mesh& mesh);
  void AdvancePhysicsStep(Mesh& mesh);
  PhysRecordedFrame BuildRecordedFrame(const Mesh& mesh, bool current) const;
  PhysGuideKeyframe* FindGuideKeyframe(int frame_index);
  const PhysGuideKeyframe* FindGuideKeyframe(int frame_index) const;
  PhysGuideKeyframe& EnsureGuideKeyframe(int frame_index);
  void RefreshActiveGuides(const Mesh& mesh);
  int FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const;
  int FindDirectiveAt(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const;
  static float DistanceToSegmentSquared(Vec2 p, Vec2 a, Vec2 b);
  void UpdateGuideFromUi(const Mesh& mesh, const GuideUiFrame& ui_frame);
  PhysGuideKeyframe& EnsureCurrentGuideKeyframe();
};
