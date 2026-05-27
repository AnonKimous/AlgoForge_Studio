#pragma once

#include "interaction_state.h"
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
  void CacheCurrentState();
  void RestoreRecordedFrame(Mesh& mesh, int state_index);
  void SetRecordedFrameExpanded(int state_index, bool expanded);
  void SetGuideKeyframeEnabled(int frame_index, bool enabled);
  void SetGuideKeyframeExpanded(int frame_index, bool expanded);
  void SetGuideKeyframeExpandedByIndex(int index, bool expanded);
  void UpsertGuideAtCurrentFrame(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target);
  void StepOnce(Mesh& mesh);
  PhysRunState run_state() const { return run_state_; }
  bool guide_enabled() const { return guide_enabled_; }
  int selected_directive() const { return selected_directive_; }
  const std::vector<DeltaMatrix>& vertex_deltas() const { return vertex_deltas_; }
  const std::vector<PhysRecordedFrame>& recorded_frames() const { return recorded_frames_; }
  const std::vector<PhysGuideKeyframe>& guide_keyframes() const { return guide_keyframes_; }
  const std::vector<PhysDirective>& active_directives() const { return solver_.directives(); }
  int current_frame_index() const { return static_cast<int>(frame_index_); }

  InteractionFrame Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel, float dt_seconds);

 private:
  int dragging_anchor_{-1};
  bool initialized_{false};
  bool pending_stop_reset_{false};
  PhysRunState run_state_{PhysRunState::Stop};
  bool guide_enabled_{true};
  int selected_directive_{-1};
  float physics_accumulator_{0.0f};
  uint64_t frame_index_{0};
  std::vector<DeltaMatrix> vertex_deltas_;
  std::vector<PhysRecordedFrame> recorded_frames_;
  std::vector<PhysGuideKeyframe> guide_keyframes_;
  std::vector<Vec3> initial_positions_;
  PhysicsSolver solver_;

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
};
