#include "phys_mode_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

void PhysModeController::PhysInit(const Mesh& mesh) {
  solver_.Init(mesh);
  initialized_ = true;
  run_state_ = PhysRunState::Pause;
  guide_enabled_ = true;
  selected_velocity_guidance_ = -1;
  physics_accumulator_ = 0.0f;
  frame_index_ = 0;
  initial_positions_ = mesh.positions;
  total_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  linear_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  angular_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  recorded_frames_.clear();
  guide_keyframes_.clear();
  selected_guide_vertices_.clear();
  recorded_frames_.push_back(BuildRecordedFrame(mesh, true));
  RefreshActiveGuides(mesh);
}

void PhysModeController::SetRunState(PhysRunState run_state) {
  run_state_ = run_state;
}

void PhysModeController::SetGuideVelocityMagnitude(float magnitude) {
  guide_velocity_magnitude_ = std::max(0.0f, magnitude);
}

void PhysModeController::SetGuideVelocitySettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames) {
  guide_velocity_magnitude_ = std::max(0.0f, magnitude);
  guide_velocity_delay_frames_ = delay_frames;
  guide_velocity_duration_frames_ = std::max(1u, duration_frames);
}

void PhysModeController::SetGuideForceSettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames) {
  guide_force_magnitude_ = std::max(0.0f, magnitude);
  guide_force_delay_frames_ = delay_frames;
  guide_force_duration_frames_ = std::max(1u, duration_frames);
}

void PhysModeController::CacheCurrentState() {
  if (!initialized_ || run_state_ != PhysRunState::Pause || recorded_frames_.empty()) return;
  PhysRecordedFrame snapshot = recorded_frames_[0];
  snapshot.current = false;
  recorded_frames_.insert(recorded_frames_.begin() + 1, std::move(snapshot));
}

void PhysModeController::RestoreRecordedFrame(Mesh& mesh, int state_index) {
  if (state_index <= 0 || state_index >= static_cast<int>(recorded_frames_.size())) return;
  const PhysRecordedFrame snapshot = recorded_frames_[state_index];
  mesh.positions = snapshot.positions;
  total_velocities_ = snapshot.total_velocities;
  linear_velocities_ = snapshot.linear_velocities;
  angular_velocities_ = snapshot.angular_velocities;
  total_velocities_.resize(mesh.positions.size(), MakeIdentityVelocity());
  linear_velocities_.resize(mesh.positions.size(), MakeIdentityVelocity());
  angular_velocities_.resize(mesh.positions.size(), MakeIdentityVelocity());
  frame_index_ = snapshot.frame_index >= 0 ? static_cast<uint64_t>(snapshot.frame_index) : 0;
  selected_velocity_guidance_ = -1;
  physics_accumulator_ = 0.0f;
  selected_guide_vertices_.clear();
  guide_ui_.ClearSelection();
  RefreshCurrentRecordedFrame(mesh);
  RefreshActiveGuides(mesh);
}

void PhysModeController::SetRecordedFrameExpanded(int state_index, bool expanded) {
  if (state_index < 0 || state_index >= static_cast<int>(recorded_frames_.size())) return;
  recorded_frames_[state_index].expanded = expanded;
}

void PhysModeController::SetGuideKeyframeEnabled(int frame_index, bool enabled) {
  PhysGuideKeyframe* keyframe = FindGuideKeyframe(frame_index);
  if (!keyframe) return;
  keyframe->enabled = enabled;
}

void PhysModeController::SetGuideKeyframeExpanded(int frame_index, bool expanded) {
  PhysGuideKeyframe* keyframe = FindGuideKeyframe(frame_index);
  if (!keyframe) return;
  keyframe->expanded = expanded;
}

void PhysModeController::SetGuideKeyframeExpandedByIndex(int index, bool expanded) {
  if (index < 0 || index >= static_cast<int>(guide_keyframes_.size())) return;
  guide_keyframes_[index].expanded = expanded;
}

void PhysModeController::UpsertVelocityGuidanceAtCurrentFrame(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target) {
  UpsertVelocityGuidanceAtCurrentFrame(mesh, std::vector<int>{vertex}, start, requested_target);
}

void PhysModeController::UpsertVelocityGuidanceAtCurrentFrame(const Mesh& mesh, const std::vector<int>& vertices, Vec3 start, Vec3 requested_target) {
  if (vertices.empty()) return;
  PhysGuideKeyframe& keyframe = EnsureCurrentGuideKeyframe();
  VelocityMatrix seeded_velocity = MakeLinearVelocityMatrix(Vec3{
    requested_target.x - start.x,
    requested_target.y - start.y,
    requested_target.z - start.z,
  });
  VelocityGuidance guidance{};
  guidance.vertex = vertices.front();
  guidance.vertices = vertices;
  guidance.start = start;
  guidance.requested_target = requested_target;
  guidance.allowed_target = requested_target;
  guidance.total_velocity = seeded_velocity;
  guidance.valid = true;

  for (VelocityGuidance& existing : keyframe.guidances) {
    if (!existing.vertices.empty() && existing.vertices == vertices) {
      existing = guidance;
      RefreshActiveGuides(mesh);
      return;
    }
    if (existing.vertex == guidance.vertex && existing.vertices.empty()) {
      existing = guidance;
      RefreshActiveGuides(mesh);
      return;
    }
  }

  keyframe.guidances.push_back(guidance);
  RefreshActiveGuides(mesh);
}

PhysRecordedFrame PhysModeController::BuildRecordedFrame(const Mesh& mesh, bool current) const {
  PhysRecordedFrame record{};
  record.frame_index = static_cast<int>(frame_index_);
  record.current = current;
  record.expanded = current ? (recorded_frames_.empty() ? true : recorded_frames_[0].expanded) : false;
  record.positions = mesh.positions;
  record.total_velocities = total_velocities_;
  record.linear_velocities = linear_velocities_;
  record.angular_velocities = angular_velocities_;
  return record;
}

PhysGuideKeyframe* PhysModeController::FindGuideKeyframe(int frame_index) {
  for (PhysGuideKeyframe& keyframe : guide_keyframes_) {
    if (keyframe.frame_index == frame_index) return &keyframe;
  }
  return nullptr;
}

const PhysGuideKeyframe* PhysModeController::FindGuideKeyframe(int frame_index) const {
  for (const PhysGuideKeyframe& keyframe : guide_keyframes_) {
    if (keyframe.frame_index == frame_index) return &keyframe;
  }
  return nullptr;
}

PhysGuideKeyframe& PhysModeController::EnsureGuideKeyframe(int frame_index) {
  for (PhysGuideKeyframe& keyframe : guide_keyframes_) {
    if (keyframe.frame_index == frame_index) return keyframe;
  }
  PhysGuideKeyframe created{};
  created.frame_index = frame_index;
  created.enabled = true;
  created.expanded = true;

  auto it = guide_keyframes_.begin();
  for (; it != guide_keyframes_.end(); ++it) {
    if (it->frame_index > frame_index) break;
  }
  it = guide_keyframes_.insert(it, std::move(created));
  return *it;
}

PhysGuideKeyframe& PhysModeController::EnsureCurrentGuideKeyframe() {
  return EnsureGuideKeyframe(static_cast<int>(frame_index_));
}

void PhysModeController::RefreshCurrentRecordedFrame(const Mesh& mesh) {
  PhysRecordedFrame current = BuildRecordedFrame(mesh, true);
  if (recorded_frames_.empty()) {
    recorded_frames_.push_back(std::move(current));
  } else {
    recorded_frames_[0] = std::move(current);
  }
}

void PhysModeController::AdvancePhysicsStep(Mesh& mesh) {
  RefreshActiveGuides(mesh);
  solver_.ApplyValidVelocityGuidances(mesh, total_velocities_, linear_velocities_, angular_velocities_);
  ++frame_index_;
  RefreshCurrentRecordedFrame(mesh);
}

void PhysModeController::ResetSimulation(Mesh& mesh) {
  if (initial_positions_.size() == mesh.positions.size()) {
    mesh.positions = initial_positions_;
  }
  solver_.Init(mesh);
  total_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  linear_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  angular_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  recorded_frames_.clear();
  guide_keyframes_.clear();
  frame_index_ = 0;
  selected_velocity_guidance_ = -1;
  physics_accumulator_ = 0.0f;
  run_state_ = PhysRunState::Pause;
  guide_enabled_ = true;
  selected_guide_vertices_.clear();
  guide_ui_.ClearSelection();
  recorded_frames_.push_back(BuildRecordedFrame(mesh, true));
  RefreshActiveGuides(mesh);
}

void PhysModeController::StepOnce(Mesh& mesh) {
  if (!initialized_) {
    PhysInit(mesh);
  }
  if (run_state_ != PhysRunState::Pause) return;
  physics_accumulator_ = 0.0f;
  AdvancePhysicsStep(mesh);
  RefreshActiveGuides(mesh);
}

void PhysModeController::Reset(Mesh& mesh) {
  if (!initialized_) {
    PhysInit(mesh);
  }
  ResetSimulation(mesh);
}

void PhysModeController::UpdateGuideFromUi(const Mesh& mesh, const GuideUiFrame& ui_frame) {
  if (ui_frame.selected_vertices.empty()) return;

  const int primary_vertex = ui_frame.selected_vertices.front();
  if (primary_vertex < 0 || static_cast<size_t>(primary_vertex) >= mesh.positions.size()) return;

  PhysGuideKeyframe& keyframe = EnsureCurrentGuideKeyframe();
  const Vec3& primary_start = mesh.positions[primary_vertex];
  Vec3 drag_delta{
    ui_frame.drag_target.x - primary_start.x,
    ui_frame.drag_target.y - primary_start.y,
    ui_frame.drag_target.z - primary_start.z,
  };
  float drag_length = std::sqrt(
    drag_delta.x * drag_delta.x +
    drag_delta.y * drag_delta.y +
    drag_delta.z * drag_delta.z);
  Vec3 drag_direction{};
  if (drag_length > 1e-6f) {
    drag_direction = Vec3{drag_delta.x / drag_length, drag_delta.y / drag_length, drag_delta.z / drag_length};
  }

  switch (guide_edit_mode_) {
    case GuideEditMode::Displacement: {
      Vec3 requested{ui_frame.drag_target.x, ui_frame.drag_target.y, primary_start.z};
      UpsertVelocityGuidanceAtCurrentFrame(mesh, ui_frame.selected_vertices, primary_start, requested);
      break;
    }
    case GuideEditMode::Velocity: {
      Vec3 velocity_vector{
        drag_direction.x * guide_velocity_magnitude_,
        drag_direction.y * guide_velocity_magnitude_,
        drag_direction.z * guide_velocity_magnitude_,
      };
      VelocityGuideVelocity guide{};
      guide.vertices = ui_frame.selected_vertices;
      guide.velocity = MakeLinearVelocityMatrix(velocity_vector);
      guide.start_frame_offset = guide_velocity_delay_frames_;
      guide.duration_frames = guide_velocity_duration_frames_;
      guide.valid = true;
      bool updated = false;
      for (VelocityGuideVelocity& existing : keyframe.guide_velocities) {
        if (existing.vertices == guide.vertices) {
          existing = guide;
          updated = true;
          break;
        }
      }
      if (!updated) {
        keyframe.guide_velocities.push_back(guide);
      }
      break;
    }
    case GuideEditMode::Force: {
      VelocityGuideForce guide{};
      guide.vertices = ui_frame.selected_vertices;
      guide.force = Vec3{
        drag_direction.x * guide_force_magnitude_,
        drag_direction.y * guide_force_magnitude_,
        drag_direction.z * guide_force_magnitude_,
      };
      guide.start_frame_offset = guide_force_delay_frames_;
      guide.duration_frames = guide_force_duration_frames_;
      guide.valid = true;
      bool updated = false;
      for (VelocityGuideForce& existing : keyframe.guide_forces) {
        if (existing.vertices == guide.vertices && existing.start_frame_offset == guide.start_frame_offset) {
          existing = guide;
          updated = true;
          break;
        }
      }
      if (!updated) {
        keyframe.guide_forces.push_back(guide);
      }
      break;
    }
  }

  selected_velocity_guidance_ = static_cast<int>(ui_frame.selected_vertices.front());
  RefreshActiveGuides(mesh);
}

void PhysModeController::RefreshActiveGuides(const Mesh& mesh) {
  if (!guide_enabled_) {
    solver_.SetVelocityGuidances(mesh, {});
    solver_.SetGuideVelocities({});
    solver_.SetGuideForces({});
    return;
  }
  const PhysGuideKeyframe* keyframe = FindGuideKeyframe(static_cast<int>(frame_index_));
  std::vector<VelocityGuidance> active_guidances;
  std::vector<VelocityGuideVelocity> active_guide_velocities;
  std::vector<VelocityGuideForce> active_guide_forces;

  if (keyframe && keyframe->enabled) {
    active_guidances = keyframe->guidances;
    for (const VelocityGuideVelocity& velocity : keyframe->guide_velocities) {
      uint64_t begin_frame = static_cast<uint64_t>(keyframe->frame_index) + velocity.start_frame_offset;
      uint64_t end_frame = begin_frame + std::max(1u, velocity.duration_frames);
      if (frame_index_ >= begin_frame && frame_index_ < end_frame) {
        active_guide_velocities.push_back(velocity);
      }
    }
  }

  for (const PhysGuideKeyframe& frame : guide_keyframes_) {
    if (!frame.enabled) continue;
    if (static_cast<uint64_t>(frame.frame_index) > frame_index_) continue;
    for (const VelocityGuideForce& force : frame.guide_forces) {
      uint64_t begin_frame = static_cast<uint64_t>(frame.frame_index) + force.start_frame_offset;
      uint64_t end_frame = begin_frame + std::max(1u, force.duration_frames);
      if (frame_index_ >= begin_frame && frame_index_ < end_frame) {
        active_guide_forces.push_back(force);
      }
    }
  }

  solver_.SetVelocityGuidances(mesh, active_guidances);
  solver_.SetGuideVelocities(active_guide_velocities);
  solver_.SetGuideForces(active_guide_forces);
  if (!keyframe || !keyframe->enabled) {
    solver_.SetVelocityGuidances(mesh, {});
    solver_.SetGuideVelocities({});
  }
}

InteractionFrame PhysModeController::Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  if (!initialized_) {
    PhysInit(mesh);
  }

  GuideUiFrame guide_ui = guide_ui_.Tick(mesh, viewport, input, mouse_pixel);
  selected_guide_vertices_ = guide_ui.selected_vertices;

  if (input.left_double_clicked) {
    int directive = FindDirectiveAt(mesh, viewport, mouse_pixel);
    if (directive >= 0) {
      selected_velocity_guidance_ = directive;
    }
  }

  if (run_state_ == PhysRunState::Run) {
    physics_accumulator_ += std::max(dt_seconds, 0.0f);
    constexpr float kPhysicsStepSeconds = 1.0f / 120.0f;
    while (physics_accumulator_ >= kPhysicsStepSeconds) {
      AdvancePhysicsStep(mesh);
      physics_accumulator_ -= kPhysicsStepSeconds;
    }
    RefreshActiveGuides(mesh);
    SelectionState selection{};
    if (!selected_guide_vertices_.empty()) {
      selection = SelectionState{SelectionKind::Vertex, selected_guide_vertices_.front(), -1};
    }
    return InteractionFrame{guide_ui.hovered_vertex, selection};
  }

  physics_accumulator_ = 0.0f;

  if (!guide_enabled_) {
    RefreshActiveGuides(mesh);
    SelectionState selection{};
    if (!selected_guide_vertices_.empty()) {
      selection = SelectionState{SelectionKind::Vertex, selected_guide_vertices_.front(), -1};
    }
    return InteractionFrame{guide_ui.hovered_vertex, selection};
  }

  if (guide_ui.dragging && !guide_ui.selected_vertices.empty()) {
    UpdateGuideFromUi(mesh, guide_ui);
  }

  RefreshActiveGuides(mesh);
  RefreshCurrentRecordedFrame(mesh);
  SelectionState selection{};
  if (!selected_guide_vertices_.empty()) {
    selection = SelectionState{SelectionKind::Vertex, selected_guide_vertices_.front(), -1};
  }
  return InteractionFrame{guide_ui.hovered_vertex, selection};
}

int PhysModeController::FindHoveredVertex(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const {
  int highlighted = -1;
  float best_pixels2 = 22.0f * 22.0f;
  for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
    Vec2 vertex_pixel = viewport.NdcToWindow(Vec2{mesh.positions[i].x, mesh.positions[i].y});
    float dx = vertex_pixel.x - mouse_pixel.x;
    float dy = vertex_pixel.y - mouse_pixel.y;
    float dist2 = dx * dx + dy * dy;
    if (dist2 <= best_pixels2) {
      best_pixels2 = dist2;
      highlighted = static_cast<int>(i);
    }
  }
  return highlighted;
}

float PhysModeController::DistanceToSegmentSquared(Vec2 p, Vec2 a, Vec2 b) {
  Vec2 ab{b.x - a.x, b.y - a.y};
  Vec2 ap{p.x - a.x, p.y - a.y};
  float ab_len2 = ab.x * ab.x + ab.y * ab.y;
  if (ab_len2 <= 1e-6f) {
    float dx = p.x - a.x;
    float dy = p.y - a.y;
    return dx * dx + dy * dy;
  }
  float t = (ap.x * ab.x + ap.y * ab.y) / ab_len2;
  t = std::clamp(t, 0.0f, 1.0f);
  Vec2 closest{a.x + ab.x * t, a.y + ab.y * t};
  float dx = p.x - closest.x;
  float dy = p.y - closest.y;
  return dx * dx + dy * dy;
}

int PhysModeController::FindDirectiveAt(const Mesh& mesh, const ViewportTransform& viewport, Vec2 mouse_pixel) const {
  (void)mesh;
  const auto& guidances = solver_.guidances();
  int hovered = -1;
  float best_pixels2 = 12.0f * 12.0f;
  for (int i = 0; i < static_cast<int>(guidances.size()); ++i) {
    const VelocityGuidance& guidance = guidances[i];
    if (guidance.vertex < 0) continue;
    if (guidance.hidden) continue;
    Vec2 start = viewport.NdcToWindow(Vec2{guidance.start.x, guidance.start.y});
    Vec2 requested = viewport.NdcToWindow(Vec2{guidance.requested_target.x, guidance.requested_target.y});
    Vec2 allowed = viewport.NdcToWindow(Vec2{guidance.allowed_target.x, guidance.allowed_target.y});
    float dist2 = DistanceToSegmentSquared(mouse_pixel, start, allowed);
    if (!guidance.valid) {
      dist2 = std::min(dist2, DistanceToSegmentSquared(mouse_pixel, start, requested));
    }
    if (dist2 <= best_pixels2) {
      best_pixels2 = dist2;
      hovered = i;
    }
  }
  return hovered;
}
