#include "validation_bridge.h"

#include "../physics/physics_types.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

ValidationVec3 ToValidationVec3(const Vec3& v) {
  return ValidationVec3{v.x, v.y, v.z};
}

ValidationMatrix4 ToValidationMatrix4(const DeltaMatrix& matrix) {
  ValidationMatrix4 out{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      out.m[row][col] = matrix(row, col);
    }
  }
  return out;
}

ValidationInteractionMode ToValidationMode(InteractionMode mode) {
  switch (mode) {
    case InteractionMode::Edit: return ValidationInteractionMode::Edit;
    case InteractionMode::Phys: return ValidationInteractionMode::Phys;
  }
  return ValidationInteractionMode::Edit;
}

ValidationPhysRunState ToValidationRunState(PhysRunState state) {
  switch (state) {
    case PhysRunState::Run: return ValidationPhysRunState::Run;
    case PhysRunState::Pause: return ValidationPhysRunState::Pause;
  }
  return ValidationPhysRunState::Pause;
}

ValidationSelectionKind ToValidationSelectionKind(SelectionKind kind) {
  switch (kind) {
    case SelectionKind::None: return ValidationSelectionKind::None;
    case SelectionKind::Vertex: return ValidationSelectionKind::Vertex;
    case SelectionKind::Triangle: return ValidationSelectionKind::Triangle;
  }
  return ValidationSelectionKind::None;
}

float TriangleArea(const Mesh& mesh, const std::array<uint32_t, 3>& tri) {
  if (tri[0] >= mesh.positions.size() || tri[1] >= mesh.positions.size() || tri[2] >= mesh.positions.size()) return 0.0f;
  const Vec3& a = mesh.positions[tri[0]];
  const Vec3& b = mesh.positions[tri[1]];
  const Vec3& c = mesh.positions[tri[2]];
  Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
  Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
  Vec3 cross{
    ab.y * ac.z - ab.z * ac.y,
    ab.z * ac.x - ab.x * ac.z,
    ab.x * ac.y - ab.y * ac.x,
  };
  return 0.5f * std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
}

float TriangleArea(const Mesh& mesh, const Mesh& reference_mesh, size_t tri_index) {
  if (tri_index >= mesh.triangles.size()) return 0.0f;
  const auto& tri = mesh.triangles[tri_index];
  if (tri_index < reference_mesh.triangles.size()) {
    return TriangleArea(reference_mesh, reference_mesh.triangles[tri_index]);
  }
  return TriangleArea(mesh, tri);
}

}  // namespace

namespace interaction_analysis {

ValidationFrameSnapshot BuildValidationFrameSnapshot(
  const Mesh& mesh,
  const Mesh& reference_mesh,
  const TriangleOrientationState& orientation_state,
  const RenderUiState& ui_state,
  const RenderFrameResult& frame_result,
  int highlighted_vertex,
  float frame_dt_seconds) {
  ValidationFrameSnapshot snapshot{};
  snapshot.frame_index = ui_state.phys_current_frame_index;
  snapshot.mode = ToValidationMode(ui_state.mode);
  snapshot.run_state = ToValidationRunState(ui_state.phys_run_state);
  snapshot.guide_enabled = ui_state.phys_guide_enabled;
  snapshot.guide_velocity_magnitude = ui_state.guide_velocity_magnitude;
  snapshot.guide_velocity_delay_frames = ui_state.guide_velocity_delay_frames;
  snapshot.guide_velocity_duration_frames = ui_state.guide_velocity_duration_frames;
  snapshot.guide_force_magnitude = ui_state.guide_force_magnitude;
  snapshot.guide_force_delay_frames = ui_state.guide_force_delay_frames;
  snapshot.guide_force_duration_frames = ui_state.guide_force_duration_frames;
  snapshot.highlighted_vertex = highlighted_vertex;
  snapshot.draw_calls = frame_result.draw_calls;
  snapshot.frame_dt_seconds = frame_dt_seconds;
  snapshot.animation_time = ui_state.animation_time;
  snapshot.selection.kind = ToValidationSelectionKind(ui_state.selection.kind);
  snapshot.selection.vertex = ui_state.selection.vertex;
  snapshot.selection.triangle = ui_state.selection.triangle;
  snapshot.selected_vertex_position = ToValidationVec3(ui_state.selected_vertex_position);
  snapshot.selected_triangle_material_gpa = ui_state.selected_triangle_material_gpa;

  snapshot.mesh.vertex_count = mesh.positions.size();
  snapshot.mesh.triangle_count = mesh.triangles.size();
  snapshot.mesh.positions.reserve(mesh.positions.size());
  snapshot.mesh.normals.reserve(mesh.normals.size());
  snapshot.mesh.triangles = mesh.triangles;
  snapshot.mesh.triangle_material_gpa = mesh.triangle_material_gpa;
  for (const Vec3& position : mesh.positions) {
    snapshot.mesh.positions.push_back(ToValidationVec3(position));
  }
  for (const Vec3& normal : mesh.normals) {
    snapshot.mesh.normals.push_back(ToValidationVec3(normal));
  }

  snapshot.physics.total_velocities.reserve(ui_state.total_velocities.size());
  for (const VelocityMatrix& velocity : ui_state.total_velocities) {
    snapshot.physics.total_velocities.push_back(ToValidationMatrix4(velocity));
  }

  snapshot.physics.linear_velocities.reserve(ui_state.linear_velocities.size());
  for (const VelocityMatrix& velocity : ui_state.linear_velocities) {
    snapshot.physics.linear_velocities.push_back(ToValidationMatrix4(velocity));
  }

  snapshot.physics.angular_velocities.reserve(ui_state.angular_velocities.size());
  for (const VelocityMatrix& velocity : ui_state.angular_velocities) {
    snapshot.physics.angular_velocities.push_back(ToValidationMatrix4(velocity));
  }

  snapshot.physics.active_velocity_guidances.reserve(ui_state.active_velocity_guidances.size());
  for (const VelocityGuidance& guidance : ui_state.active_velocity_guidances) {
    ValidationVelocityGuidance converted{};
    converted.vertex = guidance.vertex;
    converted.vertices = guidance.vertices;
    converted.hidden = guidance.hidden;
    converted.valid = guidance.valid;
    converted.start = ToValidationVec3(guidance.start);
    converted.requested_target = ToValidationVec3(guidance.requested_target);
    converted.allowed_target = ToValidationVec3(guidance.allowed_target);
    converted.total_velocity = ToValidationMatrix4(guidance.total_velocity);
    snapshot.physics.active_velocity_guidances.push_back(converted);
  }

  snapshot.physics.active_guide_velocities.reserve(ui_state.active_guide_velocities.size());
  for (const VelocityGuideVelocity& guide_velocity : ui_state.active_guide_velocities) {
    ValidationGuideVelocity converted{};
    converted.vertices = guide_velocity.vertices;
    converted.hidden = guide_velocity.hidden;
    converted.valid = guide_velocity.valid;
    converted.velocity = ToValidationMatrix4(guide_velocity.velocity);
    converted.start_frame_offset = guide_velocity.start_frame_offset;
    converted.duration_frames = guide_velocity.duration_frames;
    snapshot.physics.active_guide_velocities.push_back(converted);
  }

  snapshot.physics.active_guide_forces.reserve(ui_state.active_guide_forces.size());
  for (const VelocityGuideForce& guide_force : ui_state.active_guide_forces) {
    ValidationGuideForce converted{};
    converted.vertices = guide_force.vertices;
    converted.hidden = guide_force.hidden;
    converted.valid = guide_force.valid;
    converted.force = ToValidationVec3(guide_force.force);
    converted.start_frame_offset = guide_force.start_frame_offset;
    converted.duration_frames = guide_force.duration_frames;
    snapshot.physics.active_guide_forces.push_back(converted);
  }

  snapshot.physics.recorded_frames.reserve(ui_state.recorded_frames.size());
  for (const PhysRecordedFrame& frame : ui_state.recorded_frames) {
    ValidationRecordedFrame converted{};
    converted.frame_index = frame.frame_index;
    converted.current = frame.current;
    converted.expanded = frame.expanded;
    converted.position_count = frame.positions.size();
    converted.total_velocity_count = frame.total_velocities.size();
    converted.linear_velocity_count = frame.linear_velocities.size();
    converted.angular_velocity_count = frame.angular_velocities.size();
    snapshot.physics.recorded_frames.push_back(converted);
  }

  snapshot.physics.guide_keyframes.reserve(ui_state.guide_keyframes.size());
  for (const PhysGuideKeyframe& keyframe : ui_state.guide_keyframes) {
    ValidationGuideKeyframe converted{};
    converted.frame_index = keyframe.frame_index;
    converted.enabled = keyframe.enabled;
    converted.expanded = keyframe.expanded;
    converted.guidance_count = keyframe.guidances.size();
    snapshot.physics.guide_keyframes.push_back(converted);
  }

  const auto& matrices = orientation_state.matrices;
  const auto& faces = orientation_state.triangle_faces_viewer;
  snapshot.analysis.triangles.reserve(mesh.triangles.size());

  float min_area = std::numeric_limits<float>::max();
  float min_area_ratio = std::numeric_limits<float>::max();
  uint32_t degenerate_count = 0;
  uint32_t negative_count = 0;
  float max_delta_translation = 0.0f;

  for (size_t i = 0; i < mesh.triangles.size(); ++i) {
    const auto& tri = mesh.triangles[i];
    float area = TriangleArea(mesh, tri);
    float rest_area = TriangleArea(mesh, reference_mesh, i);
    float ratio = rest_area > 1e-8f ? area / rest_area : 0.0f;
    float determinant = i < matrices.size() ? matrices[i].determinant : 0.0f;
    bool faces_viewer = i < faces.size() ? faces[i] : false;
    float material = i < mesh.triangle_material_gpa.size() ? mesh.triangle_material_gpa[i] : 0.0f;

    min_area = std::min(min_area, area);
    min_area_ratio = std::min(min_area_ratio, ratio);
    if (area <= 1e-8f) ++degenerate_count;
    if (determinant <= 0.0f) ++negative_count;

    ValidationTriangleAnalysis tri_snapshot{};
    tri_snapshot.index = i;
    tri_snapshot.area = area;
    tri_snapshot.rest_area = rest_area;
    tri_snapshot.area_ratio = ratio;
    tri_snapshot.determinant = determinant;
    tri_snapshot.faces_viewer = faces_viewer;
    tri_snapshot.material_gpa = material;
    snapshot.analysis.triangles.push_back(tri_snapshot);
  }

  for (const VelocityMatrix& velocity : ui_state.linear_velocities) {
    Vec3 translation = ExtractLinearVelocity(velocity);
    float magnitude = std::sqrt(translation.x * translation.x + translation.y * translation.y + translation.z * translation.z);
    max_delta_translation = std::max(max_delta_translation, magnitude);
  }

  if (min_area == std::numeric_limits<float>::max()) min_area = 0.0f;
  if (min_area_ratio == std::numeric_limits<float>::max()) min_area_ratio = 0.0f;
  snapshot.analysis.min_area = min_area;
  snapshot.analysis.min_area_ratio = min_area_ratio;
  snapshot.analysis.degenerate_triangle_count = degenerate_count;
  snapshot.analysis.negative_triangle_count = negative_count;
  snapshot.analysis.max_delta_translation = max_delta_translation;

  return snapshot;
}

}  // namespace interaction_analysis
