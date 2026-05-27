#include "physics_solver.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPhysicsFps = 120.0f;
constexpr float kDt = 1.0f / kPhysicsFps;
constexpr float kTensionRate = 18.0f;
constexpr float kTensionAlpha = kTensionRate * kDt;
constexpr float kVelocityDecay = 0.94f;
constexpr float kEpsilon = 1e-5f;
constexpr float kBoundaryLimit = 0.495f;
constexpr float kBaseStrainLimit = 0.85f;
constexpr float kReferenceMaterialGpa = 210.0f;
constexpr float kRigidMaterialGpa = 1000000.0f;

bool IsMoreLowerLeft(const Vec3& candidate, const Vec3& current) {
  float candidate_score = candidate.x + candidate.y;
  float current_score = current.x + current.y;
  if (candidate_score != current_score) return candidate_score < current_score;
  if (candidate.y != current.y) return candidate.y < current.y;
  return candidate.x < current.x;
}

Vec3 Add(Vec3 a, Vec3 b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Sub(Vec3 a, Vec3 b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Mul(Vec3 v, float scale) {
  return Vec3{v.x * scale, v.y * scale, v.z * scale};
}

float Length2(Vec3 v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

float ClampMaterialGpa(float material_gpa) {
  if (!(std::isfinite(material_gpa) && material_gpa > 0.0f)) {
    return kRigidMaterialGpa;
  }
  return material_gpa;
}

float MaterialToStrainLimit(float material_gpa) {
  float stiffness = ClampMaterialGpa(material_gpa);
  float scale = kReferenceMaterialGpa / stiffness;
  float limit = kBaseStrainLimit * scale;
  return std::clamp(limit, 0.005f, 1.0f);
}

float MaterialToTensionScale(float material_gpa) {
  float stiffness = ClampMaterialGpa(material_gpa);
  float scale = kReferenceMaterialGpa / stiffness;
  return std::clamp(scale, 0.15f, 3.0f);
}

bool IsInsideBoundary(const Vec3& p) {
  return std::fabs(p.x) <= kBoundaryLimit && std::fabs(p.y) <= kBoundaryLimit;
}

}  // namespace

void PhysicsSolver::Init(const Mesh& mesh) {
  initial_mesh_ = mesh;
  rest_matrices_.clear();
  rest_matrices_.reserve(mesh.triangles.size());
  directives_.clear();

  for (uint32_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
    const auto& tri = mesh.triangles[tri_index];
    uint32_t origin_slot = 0;
    for (uint32_t slot = 1; slot < 3; ++slot) {
      if (IsMoreLowerLeft(mesh.positions[tri[slot]], mesh.positions[tri[origin_slot]])) {
        origin_slot = slot;
      }
    }

    uint32_t origin = tri[origin_slot];
    uint32_t first = tri[(origin_slot + 1) % 3];
    uint32_t second = tri[(origin_slot + 2) % 3];
    const Vec3& o = mesh.positions[origin];
    const Vec3& a = mesh.positions[first];
    const Vec3& b = mesh.positions[second];
    float m00 = a.x - o.x;
    float m10 = a.y - o.y;
    float m01 = b.x - o.x;
    float m11 = b.y - o.y;
    float det = m00 * m11 - m01 * m10;
    if (std::fabs(det) < 1e-6f) det = det < 0.0f ? -1e-6f : 1e-6f;

    PhysicsRestTriangle rest{};
    rest.origin = origin;
    rest.first = first;
    rest.second = second;
    rest.inv00 = m11 / det;
    rest.inv01 = -m01 / det;
    rest.inv10 = -m10 / det;
    rest.inv11 = m00 / det;
    if (tri_index < mesh.triangle_material_gpa.size()) {
      rest.material_gpa = ClampMaterialGpa(mesh.triangle_material_gpa[tri_index]);
    }
    rest_matrices_.push_back(rest);
  }

  initialized_ = true;
}

void PhysicsSolver::UpsertDirective(const Mesh& mesh, int vertex, Vec3 requested_target) {
  UpsertDirective(mesh, vertex, mesh.positions[vertex], requested_target);
}

void PhysicsSolver::UpsertDirective(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target) {
  PhysDirective directive{};
  directive.vertex = vertex;
  directive.start = start;
  directive.requested_target = requested_target;
  directive.allowed_target = requested_target;
  directive.delta = MakeIdentityDelta();
  directive.valid = true;

  for (PhysDirective& existing : directives_) {
    if (existing.vertex == vertex) {
      directive.hidden = existing.hidden;
      existing = directive;
      RevalidateDirectives(mesh);
      return;
    }
  }

  directives_.push_back(directive);
  RevalidateDirectives(mesh);
}

int PhysicsSolver::ClampDirectiveInsertionIndex(int index) const {
  if (index < 0) return 0;
  if (index > static_cast<int>(directives_.size())) return static_cast<int>(directives_.size());
  return index;
}

void PhysicsSolver::MoveDirective(const Mesh& mesh, int from, int to) {
  if (directives_.empty()) return;
  if (from < 0 || from >= static_cast<int>(directives_.size())) return;
  to = ClampDirectiveInsertionIndex(to);
  if (from == to) return;

  PhysDirective moved = directives_[from];
  directives_.erase(directives_.begin() + from);
  if (to > from) {
    --to;
  }
  directives_.insert(directives_.begin() + to, moved);
  RevalidateDirectives(mesh);
}

void PhysicsSolver::SetDirectiveHidden(int index, bool hidden) {
  if (index < 0 || index >= static_cast<int>(directives_.size())) return;
  directives_[index].hidden = hidden;
}

void PhysicsSolver::SetDirectives(const Mesh& mesh, const std::vector<PhysDirective>& directives) {
  directives_ = directives;
  RevalidateDirectives(mesh);
}

void PhysicsSolver::ApplyValidDirectives(Mesh& mesh, std::vector<DeltaMatrix>& vertex_deltas) {
  PhysicsStepOutput output = Solve(BuildStepInput(mesh, vertex_deltas));
  mesh.positions = output.positions;
  vertex_deltas = output.vertex_deltas;
  directives_ = output.directives;
}

void PhysicsSolver::RevalidateDirectives(const Mesh& mesh) {
  std::vector<DeltaMatrix> neutral_deltas(mesh.positions.size(), MakeIdentityDelta());
  PhysicsStepOutput output = Solve(BuildStepInput(mesh, neutral_deltas));
  directives_ = output.directives;
}

PhysicsStepInput PhysicsSolver::BuildStepInput(const Mesh& mesh, const std::vector<DeltaMatrix>& vertex_deltas) const {
  PhysicsStepInput input{};
  input.positions = mesh.positions;
  input.vertex_deltas = vertex_deltas;
  if (input.vertex_deltas.size() < input.positions.size()) {
    input.vertex_deltas.resize(input.positions.size(), MakeIdentityDelta());
  }
  input.rest_triangles = rest_matrices_;
  input.directives = directives_;
  for (size_t i = 0; i < input.rest_triangles.size(); ++i) {
    if (i < mesh.triangle_material_gpa.size()) {
      input.rest_triangles[i].material_gpa = ClampMaterialGpa(mesh.triangle_material_gpa[i]);
    }
  }
  return input;
}

PhysicsStepOutput PhysicsSolver::Solve(const PhysicsStepInput& input) const {
  PhysicsStepOutput output{};
  output.positions = input.positions;
  output.vertex_deltas = input.vertex_deltas;
  if (output.vertex_deltas.size() < output.positions.size()) {
    output.vertex_deltas.resize(output.positions.size(), MakeIdentityDelta());
  }
  output.directives = input.directives;

  std::vector<bool> guided(output.positions.size(), false);
  std::vector<DeltaMatrix> guide_deltas(output.positions.size(), MakeIdentityDelta());

  for (PhysDirective& directive : output.directives) {
    if (directive.vertex < 0 || static_cast<size_t>(directive.vertex) >= output.positions.size()) {
      directive.valid = false;
      directive.delta = MakeIdentityDelta();
      continue;
    }
    guided[directive.vertex] = true;
    directive.start = input.positions[directive.vertex];
    guide_deltas[directive.vertex] = MakeTranslationDelta(Sub(directive.requested_target, input.positions[directive.vertex]));
  }

  for (size_t i = 0; i < output.positions.size(); ++i) {
    Vec3 current = input.positions[i];
    Vec3 requested = ApplyDeltaToPoint(input.vertex_deltas[i], current);
    output.positions[i] = requested;
    if (!IsMeshStateAllowed(output.positions, input.rest_triangles)) {
      output.positions[i] = current;
      Vec3 allowed = FindNearestAllowedTarget(output.positions, input.rest_triangles, static_cast<int>(i), current, requested);
      output.positions[i] = allowed;
      if (!IsMeshStateAllowed(output.positions, input.rest_triangles)) {
        output.positions[i] = current;
      }
    }
  }

  for (PhysDirective& directive : output.directives) {
    if (directive.vertex < 0 || static_cast<size_t>(directive.vertex) >= output.positions.size()) {
      continue;
    }
    Vec3 current = input.positions[directive.vertex];
    Vec3 applied = output.positions[directive.vertex];
    directive.allowed_target = applied;
    directive.delta = MakeTranslationDelta(Sub(applied, current));
    directive.valid = Length2(Sub(applied, directive.requested_target)) <= kEpsilon * kEpsilon;
  }

  for (size_t i = 0; i < output.positions.size(); ++i) {
    if (guided[i]) {
      output.vertex_deltas[i] = guide_deltas[i];
      continue;
    }
    Vec3 step = Sub(output.positions[i], input.positions[i]);
    output.vertex_deltas[i] = MakeTranslationDelta(Mul(step, kVelocityDecay)) * input.vertex_deltas[i];
  }

  PropagateTriangleTension(input, output, guided);
  for (PhysDirective& directive : output.directives) {
    if (directive.vertex < 0 || static_cast<size_t>(directive.vertex) >= output.vertex_deltas.size()) {
      continue;
    }
    directive.delta = output.vertex_deltas[directive.vertex];
  }
  return output;
}

bool PhysicsSolver::IsMeshStateAllowed(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles) const {
  for (const Vec3& p : positions) {
    if (!IsInsideBoundary(p)) return false;
  }
  for (const PhysicsRestTriangle& rest : rest_triangles) {
    if (!IsTriangleAllowed(positions, rest)) return false;
  }
  return true;
}

bool PhysicsSolver::IsTriangleAllowed(const std::vector<Vec3>& positions, const PhysicsRestTriangle& rest) const {
  const Vec3& o = positions[rest.origin];
  const Vec3& a = positions[rest.first];
  const Vec3& b = positions[rest.second];

  float d00 = a.x - o.x;
  float d10 = a.y - o.y;
  float d01 = b.x - o.x;
  float d11 = b.y - o.y;
  float det = d00 * d11 - d01 * d10;
  if (det <= 1e-5f) return false;

  float f00 = d00 * rest.inv00 + d01 * rest.inv10;
  float f01 = d00 * rest.inv01 + d01 * rest.inv11;
  float f10 = d10 * rest.inv00 + d11 * rest.inv10;
  float f11 = d10 * rest.inv01 + d11 * rest.inv11;

  float c00 = f00 * f00 + f10 * f10;
  float c01 = f00 * f01 + f10 * f11;
  float c11 = f01 * f01 + f11 * f11;
  float e00 = (c00 - 1.0f) * 0.5f;
  float e01 = c01 * 0.5f;
  float e11 = (c11 - 1.0f) * 0.5f;
  float strain_norm = std::sqrt(e00 * e00 + 2.0f * e01 * e01 + e11 * e11);
  return strain_norm <= MaterialToStrainLimit(rest.material_gpa);
}

Vec3 PhysicsSolver::FindNearestAllowedTarget(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles, int vertex, Vec3 start, Vec3 requested) const {
  Vec3 best = start;
  float lo = 0.0f;
  float hi = 1.0f;
  for (int i = 0; i < 20; ++i) {
    float t = (lo + hi) * 0.5f;
    Vec3 candidate{
      start.x + (requested.x - start.x) * t,
      start.y + (requested.y - start.y) * t,
      start.z + (requested.z - start.z) * t,
    };
    std::vector<Vec3> trial = positions;
    trial[vertex] = candidate;
    if (IsMeshStateAllowed(trial, rest_triangles)) {
      best = candidate;
      lo = t;
    } else {
      hi = t;
    }
  }
  return best;
}

void PhysicsSolver::PropagateTriangleTension(const PhysicsStepInput& input, PhysicsStepOutput& output, const std::vector<bool>& guided) const {
  if (input.positions.size() != output.positions.size()) return;

  std::vector<Vec3> accumulated(output.positions.size());

  for (const PhysicsRestTriangle& tri : input.rest_triangles) {
    uint32_t ids[3] = {tri.origin, tri.first, tri.second};
    Vec3 moved_sum{};
    uint32_t moved_count = 0;
    for (uint32_t id : ids) {
      Vec3 delta = Sub(output.positions[id], input.positions[id]);
      if (Length2(delta) > kEpsilon * kEpsilon) {
        moved_sum = Add(moved_sum, delta);
        ++moved_count;
      }
    }
    if (moved_count == 0) continue;

    Vec3 triangle_pull = Mul(moved_sum, 1.0f / static_cast<float>(moved_count));
    triangle_pull = Mul(triangle_pull, kTensionAlpha * MaterialToTensionScale(tri.material_gpa));
    for (uint32_t id : ids) {
      if (guided[id]) continue;
      accumulated[id] = Add(accumulated[id], triangle_pull);
    }
  }

  for (uint32_t i = 0; i < output.positions.size(); ++i) {
    if (Length2(accumulated[i]) <= kEpsilon * kEpsilon) continue;
    output.vertex_deltas[i] = MakeTranslationDelta(accumulated[i]) * output.vertex_deltas[i];
  }
}
