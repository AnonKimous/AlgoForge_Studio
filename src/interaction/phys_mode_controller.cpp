#include "phys_mode_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

void PhysModeController::PhysInit(const Mesh& mesh) {
  solver_.Init(mesh);
  initialized_ = true;
  pending_stop_reset_ = false;
  run_state_ = PhysRunState::Stop;
  guide_enabled_ = true;
  dragging_anchor_ = -1;
  selected_directive_ = -1;
  physics_accumulator_ = 0.0f;
  frame_index_ = 0;
  initial_positions_ = mesh.positions;
  vertex_deltas_.assign(mesh.positions.size(), MakeIdentityDelta());
  recorded_frames_.clear();
  guide_keyframes_.clear();
  recorded_frames_.push_back(BuildRecordedFrame(mesh, true));
  RefreshActiveGuides(mesh);
}

void PhysModeController::SetRunState(PhysRunState run_state) {
  if (run_state_ != run_state && run_state == PhysRunState::Stop) {
    pending_stop_reset_ = true;
  }
  run_state_ = run_state;
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
  vertex_deltas_ = snapshot.vertex_deltas;
  vertex_deltas_.resize(mesh.positions.size(), MakeIdentityDelta());
  frame_index_ = snapshot.frame_index >= 0 ? static_cast<uint64_t>(snapshot.frame_index) : 0;
  selected_directive_ = -1;
  dragging_anchor_ = -1;
  physics_accumulator_ = 0.0f;
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

void PhysModeController::UpsertGuideAtCurrentFrame(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target) {
  PhysGuideKeyframe& keyframe = EnsureGuideKeyframe(static_cast<int>(frame_index_));
  DeltaMatrix seeded_delta = MakeTranslationDelta(Vec3{
    requested_target.x - start.x,
    requested_target.y - start.y,
    requested_target.z - start.z,
  });
  PhysDirective directive{};
  directive.vertex = vertex;
  directive.start = start;
  directive.requested_target = requested_target;
  directive.allowed_target = requested_target;
  directive.delta = seeded_delta;
  directive.valid = true;

  for (PhysDirective& existing : keyframe.directives) {
    if (existing.vertex == vertex) {
      existing = directive;
      RefreshActiveGuides(mesh);
      return;
    }
  }

  keyframe.directives.push_back(directive);
  RefreshActiveGuides(mesh);
}

PhysRecordedFrame PhysModeController::BuildRecordedFrame(const Mesh& mesh, bool current) const {
  PhysRecordedFrame record{};
  record.frame_index = static_cast<int>(frame_index_);
  record.current = current;
  record.expanded = current ? (recorded_frames_.empty() ? true : recorded_frames_[0].expanded) : false;
  record.positions = mesh.positions;
  record.vertex_deltas = vertex_deltas_;
  if (record.vertex_deltas.size() < record.positions.size()) {
    record.vertex_deltas.resize(record.positions.size(), MakeIdentityDelta());
  }
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
  solver_.ApplyValidDirectives(mesh, vertex_deltas_);
  ++frame_index_;
  RefreshCurrentRecordedFrame(mesh);
}

void PhysModeController::ResetSimulation(Mesh& mesh) {
  if (initial_positions_.size() == mesh.positions.size()) {
    mesh.positions = initial_positions_;
  }
  solver_.Init(mesh);
  vertex_deltas_.assign(mesh.positions.size(), MakeIdentityDelta());
  recorded_frames_.clear();
  guide_keyframes_.clear();
  frame_index_ = 0;
  selected_directive_ = -1;
  dragging_anchor_ = -1;
  physics_accumulator_ = 0.0f;
  pending_stop_reset_ = false;
  recorded_frames_.push_back(BuildRecordedFrame(mesh, true));
  RefreshActiveGuides(mesh);
}

void PhysModeController::StepOnce(Mesh& mesh) {
  if (!initialized_) {
    PhysInit(mesh);
  }
  if (pending_stop_reset_) {
    ResetSimulation(mesh);
  }
  if (run_state_ != PhysRunState::Pause) return;
  physics_accumulator_ = 0.0f;
  AdvancePhysicsStep(mesh);
  RefreshActiveGuides(mesh);
}

void PhysModeController::RefreshActiveGuides(const Mesh& mesh) {
  const PhysGuideKeyframe* keyframe = FindGuideKeyframe(static_cast<int>(frame_index_));
  if (!keyframe || !keyframe->enabled) {
    solver_.SetDirectives(mesh, {});
    return;
  }
  solver_.SetDirectives(mesh, keyframe->directives);
}

InteractionFrame PhysModeController::Tick(Mesh& mesh, const ViewportTransform& viewport, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  if (!initialized_) {
    PhysInit(mesh);
  }

  if (pending_stop_reset_) {
    ResetSimulation(mesh);
  }

  Vec2 mouse_ndc = viewport.WindowToNdc(mouse_pixel);
  int hovered_vertex = FindHoveredVertex(mesh, viewport, mouse_pixel);

  if (input.left_double_clicked) {
    int directive = FindDirectiveAt(mesh, viewport, mouse_pixel);
    if (directive >= 0) {
      selected_directive_ = directive;
    }
  }

  if (run_state_ == PhysRunState::Run) {
    dragging_anchor_ = -1;
    physics_accumulator_ += std::max(dt_seconds, 0.0f);
    constexpr float kPhysicsStepSeconds = 1.0f / 120.0f;
    while (physics_accumulator_ >= kPhysicsStepSeconds) {
      AdvancePhysicsStep(mesh);
      physics_accumulator_ -= kPhysicsStepSeconds;
    }
    RefreshActiveGuides(mesh);
    return InteractionFrame{hovered_vertex, SelectionState{}};
  }

  physics_accumulator_ = 0.0f;

  if (!guide_enabled_) {
    dragging_anchor_ = -1;
    return InteractionFrame{hovered_vertex, SelectionState{}};
  }

  if (input.left_pressed && hovered_vertex >= 0) {
    dragging_anchor_ = hovered_vertex;
  }

  if (input.left_down && dragging_anchor_ >= 0) {
    Vec3 requested{mouse_ndc.x, mouse_ndc.y, mesh.positions[dragging_anchor_].z};
    UpsertGuideAtCurrentFrame(mesh, dragging_anchor_, mesh.positions[dragging_anchor_], requested);
  }

  if (input.left_released && dragging_anchor_ >= 0) {
    dragging_anchor_ = -1;
  }

  RefreshActiveGuides(mesh);
  RefreshCurrentRecordedFrame(mesh);
  return InteractionFrame{hovered_vertex, hovered_vertex >= 0 ? SelectionState{SelectionKind::Vertex, hovered_vertex, -1} : SelectionState{}};
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
  const auto& directives = solver_.directives();
  int hovered = -1;
  float best_pixels2 = 12.0f * 12.0f;
  for (int i = 0; i < static_cast<int>(directives.size()); ++i) {
    const PhysDirective& directive = directives[i];
    if (directive.vertex < 0) continue;
    if (directive.hidden) continue;
    Vec2 start = viewport.NdcToWindow(Vec2{directive.start.x, directive.start.y});
    Vec2 requested = viewport.NdcToWindow(Vec2{directive.requested_target.x, directive.requested_target.y});
    Vec2 allowed = viewport.NdcToWindow(Vec2{directive.allowed_target.x, directive.allowed_target.y});
    float dist2 = DistanceToSegmentSquared(mouse_pixel, start, allowed);
    if (!directive.valid) {
      dist2 = std::min(dist2, DistanceToSegmentSquared(mouse_pixel, start, requested));
    }
    if (dist2 <= best_pixels2) {
      best_pixels2 = dist2;
      hovered = i;
    }
  }
  return hovered;
}
