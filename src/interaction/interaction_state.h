#pragma once

#include "../algorithm/algorithm_types.h"
#include "../math/common/vector_types.h"
#include "../physics/physics_types.h"

#include <cstdint>
#include <string>
#include <vector>

enum class InteractionMode {
  Edit,
  Phys,
};

enum class GuideEditMode {
  Displacement,
  Velocity,
  Force,
};

enum class SelectionKind {
  None,
  Vertex,
  Triangle,
};

struct SelectionState {
  SelectionKind kind{SelectionKind::None};
  int vertex{-1};
  int triangle{-1};
};

struct InteractionFrame {
  int highlighted_vertex{-1};
  SelectionState selection{};
};

struct GuideUiFrame {
  int hovered_vertex{-1};
  std::vector<int> selected_vertices;
  bool dragging{false};
  int dragging_vertex{-1};
  Vec3 drag_target{};
};

struct PhysRecordedFrame {
  int frame_index{-1};
  bool current{false};
  bool expanded{false};
  std::vector<Vec3> positions;
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
};

struct PhysGuideKeyframe {
  int frame_index{-1};
  bool enabled{true};
  bool expanded{false};
  std::vector<VelocityGuidance> guidances;
  std::vector<VelocityGuideVelocity> guide_velocities;
  std::vector<VelocityGuideForce> guide_forces;
};

struct RenderUiState {
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Pause};
  bool phys_guide_enabled{true};
  PhysSolverKind phys_solver_kind{PhysSolverKind::Cpu};
  std::string phys_algorithm_name;
  GuideEditMode guide_edit_mode{GuideEditMode::Displacement};
  float guide_velocity_magnitude{1.0f};
  uint32_t guide_velocity_delay_frames{0};
  uint32_t guide_velocity_duration_frames{1};
  float guide_force_magnitude{1.0f};
  int selected_velocity_guidance{-1};
  int phys_current_frame_index{0};
  std::string mesh_file_name;
  SelectionState selection{};
  std::vector<int> selected_guide_vertices;
  Vec3 selected_vertex_position{};
  float selected_triangle_material_gpa{0.0f};
  uint32_t guide_force_delay_frames{0};
  uint32_t guide_force_duration_frames{1};
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  std::vector<PhysRecordedFrame> recorded_frames;
  std::vector<PhysGuideKeyframe> guide_keyframes;
  std::vector<VelocityGuidance> active_velocity_guidances;
  std::vector<VelocityGuideVelocity> active_guide_velocities;
  std::vector<VelocityGuideForce> active_guide_forces;
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug;
  float animation_time{};
};

struct RenderFrameResult {
  uint32_t draw_calls{};
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Pause};
  bool phys_guide_enabled{true};
  GuideEditMode guide_edit_mode{GuideEditMode::Displacement};
  float guide_velocity_magnitude{1.0f};
  uint32_t guide_velocity_delay_frames{0};
  uint32_t guide_velocity_duration_frames{1};
  float guide_force_magnitude{1.0f};
  uint32_t guide_force_delay_frames{0};
  uint32_t guide_force_duration_frames{1};
  int selected_velocity_guidance{-1};
  bool phys_state_cache_requested{false};
  bool phys_reset_requested{false};
  bool phys_step_requested{false};
  bool phys_state_restore_requested{false};
  int phys_state_restore_index{-1};
  bool recorded_frame_toggle_requested{false};
  int recorded_frame_toggle_index{-1};
  bool recorded_frame_toggle_expanded{false};
  bool guide_keyframe_toggle_requested{false};
  int guide_keyframe_toggle_index{-1};
  bool guide_keyframe_toggle_enabled{false};
  bool guide_keyframe_expand_requested{false};
  int guide_keyframe_expand_index{-1};
  bool guide_keyframe_expand_expanded{false};
  bool triangle_material_change_requested{false};
  int triangle_material_triangle{-1};
  float triangle_material_gpa{0.0f};
  bool save_requested{};
};

namespace data_protocol {
using ::GuideEditMode;
using ::GuideUiFrame;
using ::InteractionFrame;
using ::InteractionMode;
using ::PhysGuideKeyframe;
using ::PhysRecordedFrame;
using ::RenderFrameResult;
using ::RenderUiState;
using ::SelectionKind;
using ::SelectionState;
}  // namespace data_protocol
