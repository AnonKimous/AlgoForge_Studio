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

struct ValidationVelocityGuidance {
  int vertex{-1};
  std::vector<int> vertices;
  bool hidden{false};
  bool valid{false};
  ValidationVec3 start{};
  ValidationVec3 requested_target{};
  ValidationVec3 allowed_target{};
  ValidationMatrix4 total_velocity{};
};

struct ValidationGuideVelocity {
  std::vector<int> vertices;
  bool hidden{false};
  bool valid{true};
  ValidationMatrix4 velocity{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
};

struct ValidationGuideForce {
  std::vector<int> vertices;
  bool hidden{false};
  bool valid{true};
  ValidationVec3 force{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
};

struct ValidationRecordedFrame {
  int frame_index{-1};
  bool current{false};
  bool expanded{false};
  size_t position_count{};
  size_t total_velocity_count{};
  size_t linear_velocity_count{};
  size_t angular_velocity_count{};
};

struct ValidationGuideKeyframe {
  int frame_index{-1};
  bool enabled{true};
  bool expanded{false};
  size_t guidance_count{};
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
  std::vector<ValidationMatrix4> total_velocities;
  std::vector<ValidationMatrix4> linear_velocities;
  std::vector<ValidationMatrix4> angular_velocities;
  std::vector<ValidationVelocityGuidance> active_velocity_guidances;
  std::vector<ValidationGuideVelocity> active_guide_velocities;
  std::vector<ValidationGuideForce> active_guide_forces;
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
  ValidationPhysRunState run_state{ValidationPhysRunState::Pause};
  bool guide_enabled{true};
  float guide_velocity_magnitude{1.0f};
  uint32_t guide_velocity_delay_frames{0};
  uint32_t guide_velocity_duration_frames{1};
  float guide_force_magnitude{1.0f};
  uint32_t guide_force_delay_frames{0};
  uint32_t guide_force_duration_frames{1};
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
