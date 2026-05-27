#pragma once

#include <array>
#include <cstdint>
#include <vector>

enum class ValidationInteractionMode {
  Edit,
  Phys,
};

enum class ValidationSelectionKind {
  None,
  Vertex,
  Triangle,
};

enum class ValidationPhysRunState {
  Run,
  Pause,
  Stop,
};

struct ValidationVec3 {
  float x{};
  float y{};
  float z{};
};

struct ValidationMatrix4 {
  float m[4][4]{};
};

struct ValidationSelectionState {
  ValidationSelectionKind kind{ValidationSelectionKind::None};
  int vertex{-1};
  int triangle{-1};
};

struct ValidationDirective {
  int vertex{-1};
  bool hidden{false};
  bool valid{false};
  ValidationVec3 start{};
  ValidationVec3 requested_target{};
  ValidationVec3 allowed_target{};
  ValidationMatrix4 delta{};
};

struct ValidationRecordedFrame {
  int frame_index{-1};
  bool current{false};
  bool expanded{false};
  size_t position_count{};
  size_t delta_count{};
};

struct ValidationGuideKeyframe {
  int frame_index{-1};
  bool enabled{true};
  bool expanded{false};
  size_t directive_count{};
};

struct ValidationTriangleAnalysis {
  size_t index{};
  float area{};
  float rest_area{};
  float area_ratio{};
  float determinant{};
  bool faces_viewer{};
  float material_gpa{};
};

struct ValidationMeshSnapshot {
  size_t vertex_count{};
  size_t triangle_count{};
  std::vector<ValidationVec3> positions;
  std::vector<ValidationVec3> normals;
  std::vector<std::array<uint32_t, 3>> triangles;
  std::vector<float> triangle_material_gpa;
};

struct ValidationPhysicsSnapshot {
  std::vector<ValidationMatrix4> vertex_deltas;
  std::vector<ValidationDirective> active_directives;
  std::vector<ValidationRecordedFrame> recorded_frames;
  std::vector<ValidationGuideKeyframe> guide_keyframes;
};

struct ValidationAnalysisSnapshot {
  std::vector<ValidationTriangleAnalysis> triangles;
  float min_area{};
  float min_area_ratio{};
  uint32_t degenerate_triangle_count{};
  uint32_t negative_triangle_count{};
  float max_delta_translation{};
};

struct ValidationFrameSnapshot {
  uint64_t sequence{};
  int frame_index{};
  ValidationInteractionMode mode{ValidationInteractionMode::Edit};
  ValidationPhysRunState run_state{ValidationPhysRunState::Stop};
  bool guide_enabled{true};
  int highlighted_vertex{-1};
  uint32_t draw_calls{};
  float frame_dt_seconds{};
  float animation_time{};
  ValidationSelectionState selection{};
  ValidationVec3 selected_vertex_position{};
  float selected_triangle_material_gpa{};
  ValidationMeshSnapshot mesh{};
  ValidationPhysicsSnapshot physics{};
  ValidationAnalysisSnapshot analysis{};
};
