#pragma once

#include "../math_types.h"
#include "../physics/physics_types.h"

#include <cstdint>
#include <string>
#include <vector>

enum class InteractionMode {
  Edit,
  Phys,
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

struct PhysRecordedFrame {
  int frame_index{-1};
  bool current{false};
  bool expanded{false};
  std::vector<Vec3> positions;
  std::vector<DeltaMatrix> vertex_deltas;
};

struct PhysGuideKeyframe {
  int frame_index{-1};
  bool enabled{true};
  bool expanded{false};
  std::vector<PhysDirective> directives;
};

struct RenderUiState {
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Stop};
  bool phys_guide_enabled{true};
  int selected_phys_directive{-1};
  int phys_current_frame_index{0};
  std::string mesh_file_name;
  SelectionState selection{};
  Vec3 selected_vertex_position{};
  float selected_triangle_material_gpa{0.0f};
  std::vector<DeltaMatrix> vertex_deltas;
  std::vector<PhysRecordedFrame> recorded_frames;
  std::vector<PhysGuideKeyframe> guide_keyframes;
  std::vector<PhysDirective> active_phys_directives;
  float animation_time{};
};

struct RenderFrameResult {
  uint32_t draw_calls{};
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Stop};
  bool phys_guide_enabled{true};
  int selected_phys_directive{-1};
  bool phys_state_cache_requested{false};
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
