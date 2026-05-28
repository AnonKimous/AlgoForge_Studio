#include "physics_solver.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPhysicsFps = 120.0f;
constexpr float kDt = 1.0f / kPhysicsFps;
constexpr float kVelocityDecay = 0.94f;
constexpr float kAngularVelocityDecay = 0.92f;
constexpr float kEpsilon = 1e-5f;
constexpr float kBoundaryLimit = 0.495f;
constexpr float kBaseStrainLimit = 0.85f;
constexpr float kReferenceMaterialGpa = 210.0f;
constexpr float kRigidMaterialGpa = 1000000.0f;
constexpr float kMassPerVertex = 1.0f;
constexpr float kInertiaPerVertex = 1.0f;

struct Mat2 {
  float m00{};
  float m01{};
  float m10{};
  float m11{};
};

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

Vec3 Lerp(Vec3 a, Vec3 b, float t) {
  return Vec3{
    a.x + (b.x - a.x) * t,
    a.y + (b.y - a.y) * t,
    a.z + (b.z - a.z) * t,
  };
}

Vec2 ToVec2(const Vec3& v) {
  return Vec2{v.x, v.y};
}

Vec3 ToVec3(const Vec2& v, float z = 0.0f) {
  return Vec3{v.x, v.y, z};
}

Mat2 MakeMat2(float m00, float m01, float m10, float m11) {
  return Mat2{m00, m01, m10, m11};
}

Mat2 Identity2() {
  return Mat2{1.0f, 0.0f, 0.0f, 1.0f};
}

Mat2 Transpose(const Mat2& m) {
  return Mat2{m.m00, m.m10, m.m01, m.m11};
}

Mat2 Add(const Mat2& a, const Mat2& b) {
  return Mat2{a.m00 + b.m00, a.m01 + b.m01, a.m10 + b.m10, a.m11 + b.m11};
}

Mat2 Sub(const Mat2& a, const Mat2& b) {
  return Mat2{a.m00 - b.m00, a.m01 - b.m01, a.m10 - b.m10, a.m11 - b.m11};
}

Mat2 Mul(const Mat2& m, float scale) {
  return Mat2{m.m00 * scale, m.m01 * scale, m.m10 * scale, m.m11 * scale};
}

Mat2 Mul(const Mat2& a, const Mat2& b) {
  return Mat2{
    a.m00 * b.m00 + a.m01 * b.m10,
    a.m00 * b.m01 + a.m01 * b.m11,
    a.m10 * b.m00 + a.m11 * b.m10,
    a.m10 * b.m01 + a.m11 * b.m11,
  };
}

Vec2 Mul(const Mat2& m, Vec2 v) {
  return Vec2{
    m.m00 * v.x + m.m01 * v.y,
    m.m10 * v.x + m.m11 * v.y,
  };
}

float Determinant(const Mat2& m) {
  return m.m00 * m.m11 - m.m01 * m.m10;
}

Mat2 Inverse(const Mat2& m) {
  float det = Determinant(m);
  if (!std::isfinite(det) || std::fabs(det) < 1e-8f) {
    return Identity2();
  }
  float inv_det = 1.0f / det;
  return Mat2{
    m.m11 * inv_det,
    -m.m01 * inv_det,
    -m.m10 * inv_det,
    m.m00 * inv_det,
  };
}

float Cross2(Vec2 a, Vec2 b) {
  return a.x * b.y - a.y * b.x;
}

float Length2(Vec3 v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float ClampMaterialGpa(float material_gpa) {
  if (!(std::isfinite(material_gpa) && material_gpa > 0.0f)) {
    return kRigidMaterialGpa;
  }
  return material_gpa;
}

float MaterialToStiffnessScale(float material_gpa) {
  float stiffness = ClampMaterialGpa(material_gpa);
  float scale = kReferenceMaterialGpa / stiffness;
  return std::clamp(scale, 0.15f, 3.0f);
}

float MaterialToStrainLimit(float material_gpa) {
  float scale = MaterialToStiffnessScale(material_gpa);
  float limit = kBaseStrainLimit * scale;
  return std::clamp(limit, 0.005f, 1.0f);
}

float MaterialToLam(float material_gpa) {
  return 2.0f * MaterialToStiffnessScale(material_gpa);
}

float MaterialToMu(float material_gpa) {
  return 1.0f * MaterialToStiffnessScale(material_gpa);
}

Vec3 ClampToBoundary(Vec3 p) {
  p.x = std::clamp(p.x, -kBoundaryLimit, kBoundaryLimit);
  p.y = std::clamp(p.y, -kBoundaryLimit, kBoundaryLimit);
  return p;
}

bool IsInsideBoundary(const Vec3& p) {
  return std::fabs(p.x) <= kBoundaryLimit && std::fabs(p.y) <= kBoundaryLimit;
}

float TriangleConstraintSeverity(const std::vector<Vec3>& positions, const PhysicsRestTriangle& rest) {
  if (rest.origin >= positions.size() || rest.first >= positions.size() || rest.second >= positions.size()) {
    return 0.0f;
  }

  const Vec3& o = positions[rest.origin];
  const Vec3& a = positions[rest.first];
  const Vec3& b = positions[rest.second];
  float d00 = a.x - o.x;
  float d10 = a.y - o.y;
  float d01 = b.x - o.x;
  float d11 = b.y - o.y;
  float det = d00 * d11 - d01 * d10;
  if (det <= 1e-5f) return 1.0f;

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
  float limit = MaterialToStrainLimit(rest.material_gpa);
  if (strain_norm <= limit) return 0.0f;
  float over = strain_norm - limit;
  return std::clamp(over / (strain_norm + 1e-6f), 0.0f, 1.0f);
}

float RotationAngleFromMatrix(const VelocityMatrix& matrix) {
  return std::atan2(matrix(1, 0), matrix(0, 0));
}

float RotationAngleFromF(const Mat2& f) {
  float trace = f.m00 + f.m11;
  float skew = f.m10 - f.m01;
  if (std::fabs(trace) < 1e-8f && std::fabs(skew) < 1e-8f) {
    return 0.0f;
  }
  return std::atan2(skew, trace);
}

VelocityMatrix MakeRotationVelocityMatrix(float radians) {
  VelocityMatrix velocity = VelocityMatrix::Identity();
  float c = std::cos(radians);
  float s = std::sin(radians);
  velocity(0, 0) = c;
  velocity(0, 1) = -s;
  velocity(1, 0) = s;
  velocity(1, 1) = c;
  return velocity;
}

}  // namespace

void PhysicsSolver::Init(const Mesh& mesh) {
  initial_mesh_ = mesh;
  rest_matrices_.clear();
  rest_matrices_.reserve(mesh.triangles.size());
  guidances_.clear();
  guide_velocities_.clear();
  guide_forces_.clear();

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
    rest.rest_area = 0.5f * std::fabs(det);
    if (tri_index < mesh.triangle_material_gpa.size()) {
      rest.material_gpa = ClampMaterialGpa(mesh.triangle_material_gpa[tri_index]);
    }
    rest_matrices_.push_back(rest);
  }

  initialized_ = true;
}

void PhysicsSolver::UpsertVelocityGuidance(const Mesh& mesh, int vertex, Vec3 requested_target) {
  UpsertVelocityGuidance(mesh, vertex, mesh.positions[vertex], requested_target);
}

void PhysicsSolver::UpsertVelocityGuidance(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target) {
  VelocityGuidance guidance{};
  guidance.vertex = vertex;
  guidance.start = start;
  guidance.requested_target = requested_target;
  guidance.allowed_target = requested_target;
  guidance.total_velocity = MakeIdentityVelocity();
  guidance.valid = true;

  for (VelocityGuidance& existing : guidances_) {
    if (existing.vertex == vertex) {
      guidance.hidden = existing.hidden;
      existing = guidance;
      RevalidateDirectives(mesh);
      return;
    }
  }

  guidances_.push_back(guidance);
  RevalidateDirectives(mesh);
}

int PhysicsSolver::ClampDirectiveInsertionIndex(int index) const {
  if (index < 0) return 0;
  if (index > static_cast<int>(guidances_.size())) return static_cast<int>(guidances_.size());
  return index;
}

void PhysicsSolver::MoveVelocityGuidance(const Mesh& mesh, int from, int to) {
  if (guidances_.empty()) return;
  if (from < 0 || from >= static_cast<int>(guidances_.size())) return;
  to = ClampDirectiveInsertionIndex(to);
  if (from == to) return;

  VelocityGuidance moved = guidances_[from];
  guidances_.erase(guidances_.begin() + from);
  if (to > from) {
    --to;
  }
  guidances_.insert(guidances_.begin() + to, moved);
  RevalidateDirectives(mesh);
}

void PhysicsSolver::SetVelocityGuidanceHidden(int index, bool hidden) {
  if (index < 0 || index >= static_cast<int>(guidances_.size())) return;
  guidances_[index].hidden = hidden;
}

void PhysicsSolver::SetVelocityGuidances(const Mesh& mesh, const std::vector<VelocityGuidance>& guidances) {
  guidances_ = guidances;
  RevalidateDirectives(mesh);
}

void PhysicsSolver::SetGuideVelocities(const std::vector<VelocityGuideVelocity>& guide_velocities) {
  guide_velocities_ = guide_velocities;
}

void PhysicsSolver::SetGuideForces(const std::vector<VelocityGuideForce>& guide_forces) {
  guide_forces_ = guide_forces;
}

void PhysicsSolver::ApplyValidVelocityGuidances(Mesh& mesh, std::vector<VelocityMatrix>& total_velocities, std::vector<VelocityMatrix>& linear_velocities, std::vector<VelocityMatrix>& angular_velocities) {
  PhysicsStepOutput output = Solve(BuildStepInput(mesh, total_velocities, linear_velocities, angular_velocities));
  mesh.positions = output.positions;
  total_velocities = output.total_velocities;
  linear_velocities = output.linear_velocities;
  angular_velocities = output.angular_velocities;
  guidances_ = output.guidances;
}

void PhysicsSolver::RevalidateDirectives(const Mesh& mesh) {
  std::vector<VelocityMatrix> neutral_total(mesh.positions.size(), MakeIdentityVelocity());
  std::vector<VelocityMatrix> neutral_linear(mesh.positions.size(), MakeIdentityVelocity());
  std::vector<VelocityMatrix> neutral_angular(mesh.positions.size(), MakeIdentityVelocity());
  PhysicsStepOutput output = Solve(BuildStepInput(mesh, neutral_total, neutral_linear, neutral_angular));
  guidances_ = output.guidances;
}

PhysicsStepInput PhysicsSolver::BuildStepInput(const Mesh& mesh, const std::vector<VelocityMatrix>& total_velocities, const std::vector<VelocityMatrix>& linear_velocities, const std::vector<VelocityMatrix>& angular_velocities) const {
  PhysicsStepInput input{};
  input.positions = mesh.positions;
  input.total_velocities = total_velocities;
  input.linear_velocities = linear_velocities;
  input.angular_velocities = angular_velocities;
  if (input.total_velocities.size() < input.positions.size()) {
    input.total_velocities.resize(input.positions.size(), MakeIdentityVelocity());
  }
  if (input.linear_velocities.size() < input.positions.size()) {
    input.linear_velocities.resize(input.positions.size(), MakeIdentityVelocity());
  }
  if (input.angular_velocities.size() < input.positions.size()) {
    input.angular_velocities.resize(input.positions.size(), MakeIdentityVelocity());
  }
  input.rest_triangles = rest_matrices_;
  input.guidances = guidances_;
  input.guide_velocities = guide_velocities_;
  input.guide_forces = guide_forces_;
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
  output.total_velocities = input.total_velocities;
  output.linear_velocities = input.linear_velocities;
  output.angular_velocities = input.angular_velocities;
  output.guidances = input.guidances;
  output.guide_velocities = input.guide_velocities;
  output.guide_forces = input.guide_forces;
  if (output.total_velocities.size() < output.positions.size()) {
    output.total_velocities.resize(output.positions.size(), MakeIdentityVelocity());
  }
  if (output.linear_velocities.size() < output.positions.size()) {
    output.linear_velocities.resize(output.positions.size(), MakeIdentityVelocity());
  }
  if (output.angular_velocities.size() < output.positions.size()) {
    output.angular_velocities.resize(output.positions.size(), MakeIdentityVelocity());
  }

  std::vector<bool> displacement_target_set(output.positions.size(), false);
  std::vector<Vec3> displacement_targets(output.positions.size(), Vec3{});

  for (VelocityGuidance& guidance : output.guidances) {
    std::vector<int> vertices = guidance.vertices;
    if (vertices.empty() && guidance.vertex >= 0) {
      vertices.push_back(guidance.vertex);
    }
    if (vertices.empty()) {
      guidance.valid = false;
      guidance.total_velocity = MakeIdentityVelocity();
      continue;
    }
    guidance.vertex = vertices.front();
    guidance.vertices = vertices;
    if (guidance.vertex < 0 || static_cast<size_t>(guidance.vertex) >= output.positions.size()) {
      guidance.valid = false;
      guidance.total_velocity = MakeIdentityVelocity();
      continue;
    }
    guidance.start = input.positions[guidance.vertex];
    Vec3 allowed_target = FindNearestAllowedTarget(
      input.positions,
      input.rest_triangles,
      guidance.vertex,
      input.positions[guidance.vertex],
      guidance.requested_target);
    guidance.allowed_target = allowed_target;
    Vec3 primary_delta = Sub(allowed_target, input.positions[guidance.vertex]);
    guidance.valid = Length2(Sub(allowed_target, guidance.requested_target)) <= kEpsilon * kEpsilon;
    guidance.total_velocity = MakeLinearVelocityMatrix(primary_delta);
    displacement_targets[guidance.vertex] = allowed_target;
    displacement_target_set[guidance.vertex] = true;
    for (size_t i = 1; i < guidance.vertices.size(); ++i) {
      int vertex = guidance.vertices[i];
      if (vertex < 0 || static_cast<size_t>(vertex) >= output.positions.size()) continue;
      Vec3 start = input.positions[vertex];
      Vec3 requested = Add(start, primary_delta);
      Vec3 allowed = FindNearestAllowedTarget(input.positions, input.rest_triangles, vertex, start, requested);
      if (Length2(Sub(allowed, requested)) > kEpsilon * kEpsilon) {
        guidance.valid = false;
      }
      displacement_targets[vertex] = allowed;
      displacement_target_set[vertex] = true;
    }
  }

  for (const VelocityGuideVelocity& guide_velocity : output.guide_velocities) {
    if (guide_velocity.hidden || !guide_velocity.valid) continue;
    for (int vertex : guide_velocity.vertices) {
      if (vertex < 0 || static_cast<size_t>(vertex) >= output.positions.size()) continue;
      output.total_velocities[vertex] = guide_velocity.velocity;
      output.linear_velocities[vertex] = guide_velocity.velocity;
      output.angular_velocities[vertex] = MakeIdentityVelocity();
    }
  }

  for (size_t i = 0; i < output.positions.size(); ++i) {
    Vec3 current = input.positions[i];
    output.positions[i] = ApplyVelocityToPoint(output.total_velocities[i], current);
  }

  std::vector<Vec3> moved_positions = output.positions;
  std::vector<Vec3> correction_sum(output.positions.size(), Vec3{});
  std::vector<uint32_t> correction_count(output.positions.size(), 0);

  for (size_t tri_index = 0; tri_index < input.rest_triangles.size(); ++tri_index) {
    const PhysicsRestTriangle& rest = input.rest_triangles[tri_index];
    if (rest.origin >= moved_positions.size() || rest.first >= moved_positions.size() || rest.second >= moved_positions.size()) {
      continue;
    }

    float severity = TriangleConstraintSeverity(moved_positions, rest);
    if (severity <= 0.0f) {
      continue;
    }

    const Vec3& current_o = moved_positions[rest.origin];
    const Vec3& current_a = moved_positions[rest.first];
    const Vec3& current_b = moved_positions[rest.second];
    const Vec3& rest_o = initial_mesh_.positions[rest.origin];
    const Vec3& rest_a = initial_mesh_.positions[rest.first];
    const Vec3& rest_b = initial_mesh_.positions[rest.second];

    correction_sum[rest.origin] = Add(correction_sum[rest.origin], Lerp(current_o, rest_o, severity));
    correction_sum[rest.first] = Add(correction_sum[rest.first], Lerp(current_a, rest_a, severity));
    correction_sum[rest.second] = Add(correction_sum[rest.second], Lerp(current_b, rest_b, severity));
    ++correction_count[rest.origin];
    ++correction_count[rest.first];
    ++correction_count[rest.second];
  }

  for (size_t i = 0; i < moved_positions.size(); ++i) {
    if (correction_count[i] > 0) {
      moved_positions[i] = Mul(correction_sum[i], 1.0f / static_cast<float>(correction_count[i]));
    }
    moved_positions[i] = ClampToBoundary(moved_positions[i]);
  }

  output.positions = moved_positions;

  for (size_t i = 0; i < output.positions.size(); ++i) {
    if (displacement_target_set[i]) {
      output.positions[i] = displacement_targets[i];
    }
  }

  std::vector<Vec3> linear_force(output.positions.size(), Vec3{});
  std::vector<float> angular_force(output.positions.size(), 0.0f);

  for (const VelocityGuideForce& force_guide : output.guide_forces) {
    if (force_guide.hidden || !force_guide.valid) continue;
    for (int vertex : force_guide.vertices) {
      if (vertex < 0 || static_cast<size_t>(vertex) >= output.positions.size()) continue;
      linear_force[vertex] = Add(linear_force[vertex], force_guide.force);
    }
  }

  for (size_t tri_index = 0; tri_index < input.rest_triangles.size(); ++tri_index) {
    const PhysicsRestTriangle& rest = input.rest_triangles[tri_index];
    if (rest.origin >= input.positions.size() || rest.first >= input.positions.size() || rest.second >= input.positions.size()) {
      continue;
    }

    const Vec3& p0 = output.positions[rest.origin];
    const Vec3& p1 = output.positions[rest.first];
    const Vec3& p2 = output.positions[rest.second];
    Mat2 Ds = MakeMat2(
      p1.x - p0.x,
      p2.x - p0.x,
      p1.y - p0.y,
      p2.y - p0.y);
    Mat2 DmInv = MakeMat2(rest.inv00, rest.inv01, rest.inv10, rest.inv11);
    Mat2 F = Mul(Ds, DmInv);
    float angle = RotationAngleFromF(F);
    Mat2 R = MakeMat2(std::cos(angle), -std::sin(angle), std::sin(angle), std::cos(angle));

    float J = Determinant(F);
    if (!std::isfinite(J)) J = 1.0f;
    if (std::fabs(J) < 1e-6f) J = J < 0.0f ? -1e-6f : 1e-6f;

    Mat2 FinvT = Transpose(Inverse(F));
    float mu = MaterialToMu(rest.material_gpa);
    float lambda = MaterialToLam(rest.material_gpa);
    Mat2 P = Add(Mul(Sub(F, R), 2.0f * mu), Mul(FinvT, lambda * (J - 1.0f) * J));
    Mat2 H = Mul(Mul(P, Transpose(DmInv)), -rest.rest_area);

    Vec2 f1{H.m00, H.m10};
    Vec2 f2{H.m01, H.m11};
    Vec2 f0{-(f1.x + f2.x), -(f1.y + f2.y)};

    Vec2 center{(p0.x + p1.x + p2.x) / 3.0f, (p0.y + p1.y + p2.y) / 3.0f};
    float torque = 0.0f;
    torque += Cross2(Vec2{p0.x - center.x, p0.y - center.y}, f0);
    torque += Cross2(Vec2{p1.x - center.x, p1.y - center.y}, f1);
    torque += Cross2(Vec2{p2.x - center.x, p2.y - center.y}, f2);

    const uint32_t ids[3] = {rest.origin, rest.first, rest.second};
    const Vec2 forces[3] = {f0, f1, f2};
    for (int slot = 0; slot < 3; ++slot) {
      uint32_t id = ids[slot];
      if (id >= output.positions.size()) continue;
      linear_force[id] = Add(linear_force[id], Vec3{forces[slot].x, forces[slot].y, 0.0f});
      angular_force[id] += torque / 3.0f;
    }
  }

  for (size_t i = 0; i < output.positions.size(); ++i) {
    Vec3 prev_linear_motion = ExtractLinearVelocity(output.linear_velocities[i]);
    float prev_angle = RotationAngleFromMatrix(output.angular_velocities[i]);

    Vec3 next_linear_motion = Add(
      Mul(prev_linear_motion, kVelocityDecay),
      Mul(linear_force[i], kDt * kDt / kMassPerVertex));
    float next_angle = prev_angle * kAngularVelocityDecay + angular_force[i] * (kDt * kDt / kInertiaPerVertex);
    next_angle = std::clamp(next_angle, -0.35f, 0.35f);

    output.linear_velocities[i] = MakeLinearVelocityMatrix(next_linear_motion);
    output.angular_velocities[i] = MakeRotationVelocityMatrix(next_angle);
    output.total_velocities[i] = ComposeVelocity(output.angular_velocities[i], output.linear_velocities[i]);
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

void PhysicsSolver::ApplyCorotatedResponse(const PhysicsStepInput& input, PhysicsStepOutput& output, const std::vector<bool>& guided) const {
  (void)input;
  (void)output;
  (void)guided;
}
