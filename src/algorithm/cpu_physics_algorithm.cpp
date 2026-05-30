#include "cpu_physics_algorithm.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace algorithm {

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

struct _Mat2 {
  float m00{};
  float m01{};
  float m10{};
  float m11{};
};

bool _SupportsCpuAlgorithm(const std::string& algorithm_name) {
  return algorithm_name == "legacy_corotated_cpu" || algorithm_name == "legacy_corotated";
}

Vec3 _Add(Vec3 a, Vec3 b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 _Sub(Vec3 a, Vec3 b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 _Mul(Vec3 v, float scale) {
  return Vec3{v.x * scale, v.y * scale, v.z * scale};
}

Vec3 _Lerp(Vec3 a, Vec3 b, float t) {
  return Vec3{
    a.x + (b.x - a.x) * t,
    a.y + (b.y - a.y) * t,
    a.z + (b.z - a.z) * t,
  };
}

_Mat2 _MakeMat2(float m00, float m01, float m10, float m11) {
  return _Mat2{m00, m01, m10, m11};
}

_Mat2 _Identity2() {
  return _Mat2{1.0f, 0.0f, 0.0f, 1.0f};
}

_Mat2 _Transpose(const _Mat2& m) {
  return _Mat2{m.m00, m.m10, m.m01, m.m11};
}

_Mat2 _Add(const _Mat2& a, const _Mat2& b) {
  return _Mat2{a.m00 + b.m00, a.m01 + b.m01, a.m10 + b.m10, a.m11 + b.m11};
}

_Mat2 _Sub(const _Mat2& a, const _Mat2& b) {
  return _Mat2{a.m00 - b.m00, a.m01 - b.m01, a.m10 - b.m10, a.m11 - b.m11};
}

_Mat2 _Mul(const _Mat2& m, float scale) {
  return _Mat2{m.m00 * scale, m.m01 * scale, m.m10 * scale, m.m11 * scale};
}

_Mat2 _Mul(const _Mat2& a, const _Mat2& b) {
  return _Mat2{
    a.m00 * b.m00 + a.m01 * b.m10,
    a.m00 * b.m01 + a.m01 * b.m11,
    a.m10 * b.m00 + a.m11 * b.m10,
    a.m10 * b.m01 + a.m11 * b.m11,
  };
}

Vec2 _Mul(const _Mat2& m, Vec2 v) {
  return Vec2{
    m.m00 * v.x + m.m01 * v.y,
    m.m10 * v.x + m.m11 * v.y,
  };
}

float _Determinant(const _Mat2& m) {
  return m.m00 * m.m11 - m.m01 * m.m10;
}

_Mat2 _Inverse(const _Mat2& m) {
  float det = _Determinant(m);
  if (!std::isfinite(det) || std::fabs(det) < 1e-8f) {
    return _Identity2();
  }
  float inv_det = 1.0f / det;
  return _Mat2{
    m.m11 * inv_det,
    -m.m01 * inv_det,
    -m.m10 * inv_det,
    m.m00 * inv_det,
  };
}

float _Cross2(Vec2 a, Vec2 b) {
  return a.x * b.y - a.y * b.x;
}

float _Length2(Vec3 v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

float _ClampMaterialGpa(float material_gpa) {
  if (!(std::isfinite(material_gpa) && material_gpa > 0.0f)) {
    return kRigidMaterialGpa;
  }
  return material_gpa;
}

float _MaterialToStiffnessScale(float material_gpa) {
  float stiffness = _ClampMaterialGpa(material_gpa);
  float scale = kReferenceMaterialGpa / stiffness;
  return std::clamp(scale, 0.15f, 3.0f);
}

float _MaterialToStrainLimit(float material_gpa) {
  float scale = _MaterialToStiffnessScale(material_gpa);
  float limit = kBaseStrainLimit * scale;
  return std::clamp(limit, 0.005f, 1.0f);
}

float _MaterialToLam(float material_gpa) {
  return 2.0f * _MaterialToStiffnessScale(material_gpa);
}

float _MaterialToMu(float material_gpa) {
  return 1.0f * _MaterialToStiffnessScale(material_gpa);
}

Vec3 _ClampToBoundary(Vec3 p) {
  p.x = std::clamp(p.x, -kBoundaryLimit, kBoundaryLimit);
  p.y = std::clamp(p.y, -kBoundaryLimit, kBoundaryLimit);
  return p;
}

bool _IsInsideBoundary(const Vec3& p) {
  return std::fabs(p.x) <= kBoundaryLimit && std::fabs(p.y) <= kBoundaryLimit;
}

float _TriangleConstraintSeverity(const std::vector<Vec3>& positions, const PhysicsRestTriangle& rest) {
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
  float limit = _MaterialToStrainLimit(rest.material_gpa);
  if (strain_norm <= limit) return 0.0f;
  float over = strain_norm - limit;
  return std::clamp(over / (strain_norm + 1e-6f), 0.0f, 1.0f);
}

float _RotationAngleFromMatrix(const VelocityMatrix& matrix) {
  return std::atan2(matrix(1, 0), matrix(0, 0));
}

float _RotationAngleFromF(const _Mat2& f) {
  float trace = f.m00 + f.m11;
  float skew = f.m10 - f.m01;
  if (std::fabs(trace) < 1e-8f && std::fabs(skew) < 1e-8f) {
    return 0.0f;
  }
  return std::atan2(skew, trace);
}

VelocityMatrix _MakeRotationVelocityMatrix(float radians) {
  VelocityMatrix velocity = VelocityMatrix::Identity();
  float c = std::cos(radians);
  float s = std::sin(radians);
  velocity(0, 0) = c;
  velocity(0, 1) = -s;
  velocity(1, 0) = s;
  velocity(1, 1) = c;
  return velocity;
}

bool _IsTriangleAllowed(const std::vector<Vec3>& positions, const PhysicsRestTriangle& rest) {
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
  return strain_norm <= _MaterialToStrainLimit(rest.material_gpa);
}

bool _IsMeshStateAllowed(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles) {
  for (const Vec3& p : positions) {
    if (!_IsInsideBoundary(p)) return false;
  }
  for (const PhysicsRestTriangle& rest : rest_triangles) {
    if (!_IsTriangleAllowed(positions, rest)) return false;
  }
  return true;
}

Vec3 _FindNearestAllowedTarget(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles, int vertex, Vec3 start, Vec3 requested) {
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
    if (_IsMeshStateAllowed(trial, rest_triangles)) {
      best = candidate;
      lo = t;
    } else {
      hi = t;
    }
  }
  return best;
}

PhysicsStepOutput _BuildInitialOutput(const PhysicsStepInput& input) {
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
  return output;
}

void _RunLegacyCorotated(const PhysicsStepInput& input, PhysicsStepOutput* output) {
  std::vector<bool> displacement_target_set(output->positions.size(), false);
  std::vector<Vec3> displacement_targets(output->positions.size(), Vec3{});

  for (VelocityGuidance& guidance : output->guidances) {
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
    if (guidance.vertex < 0 || static_cast<size_t>(guidance.vertex) >= output->positions.size()) {
      guidance.valid = false;
      guidance.total_velocity = MakeIdentityVelocity();
      continue;
    }
    guidance.start = input.positions[guidance.vertex];
    Vec3 allowed_target = _FindNearestAllowedTarget(
      input.positions,
      input.rest_triangles,
      guidance.vertex,
      input.positions[guidance.vertex],
      guidance.requested_target);
    guidance.allowed_target = allowed_target;
    Vec3 primary_delta = _Sub(allowed_target, input.positions[guidance.vertex]);
    guidance.valid = _Length2(_Sub(allowed_target, guidance.requested_target)) <= kEpsilon * kEpsilon;
    guidance.total_velocity = MakeLinearVelocityMatrix(primary_delta);
    displacement_targets[guidance.vertex] = allowed_target;
    displacement_target_set[guidance.vertex] = true;
    for (size_t i = 1; i < guidance.vertices.size(); ++i) {
      int vertex = guidance.vertices[i];
      if (vertex < 0 || static_cast<size_t>(vertex) >= output->positions.size()) continue;
      Vec3 start = input.positions[vertex];
      Vec3 requested = _Add(start, primary_delta);
      Vec3 allowed = _FindNearestAllowedTarget(input.positions, input.rest_triangles, vertex, start, requested);
      if (_Length2(_Sub(allowed, requested)) > kEpsilon * kEpsilon) {
        guidance.valid = false;
      }
      displacement_targets[vertex] = allowed;
      displacement_target_set[vertex] = true;
    }
  }

  for (const VelocityGuideVelocity& guide_velocity : output->guide_velocities) {
    if (guide_velocity.hidden || !guide_velocity.valid) continue;
    for (int vertex : guide_velocity.vertices) {
      if (vertex < 0 || static_cast<size_t>(vertex) >= output->positions.size()) continue;
      output->total_velocities[vertex] = guide_velocity.velocity;
      output->linear_velocities[vertex] = guide_velocity.velocity;
      output->angular_velocities[vertex] = MakeIdentityVelocity();
    }
  }

  for (size_t i = 0; i < output->positions.size(); ++i) {
    Vec3 current = input.positions[i];
    output->positions[i] = ApplyVelocityToPoint(output->total_velocities[i], current);
  }

  std::vector<Vec3> moved_positions = output->positions;
  std::vector<Vec3> correction_sum(output->positions.size(), Vec3{});
  std::vector<uint32_t> correction_count(output->positions.size(), 0);

  for (size_t tri_index = 0; tri_index < input.rest_triangles.size(); ++tri_index) {
    const PhysicsRestTriangle& rest = input.rest_triangles[tri_index];
    if (rest.origin >= moved_positions.size() || rest.first >= moved_positions.size() || rest.second >= moved_positions.size()) {
      continue;
    }

    float severity = _TriangleConstraintSeverity(moved_positions, rest);
    if (severity <= 0.0f) {
      continue;
    }

    const Vec3& current_o = moved_positions[rest.origin];
    const Vec3& current_a = moved_positions[rest.first];
    const Vec3& current_b = moved_positions[rest.second];
    const Vec3& rest_o = input.rest_positions[rest.origin];
    const Vec3& rest_a = input.rest_positions[rest.first];
    const Vec3& rest_b = input.rest_positions[rest.second];

    correction_sum[rest.origin] = _Add(correction_sum[rest.origin], _Lerp(current_o, rest_o, severity));
    correction_sum[rest.first] = _Add(correction_sum[rest.first], _Lerp(current_a, rest_a, severity));
    correction_sum[rest.second] = _Add(correction_sum[rest.second], _Lerp(current_b, rest_b, severity));
    ++correction_count[rest.origin];
    ++correction_count[rest.first];
    ++correction_count[rest.second];
  }

  for (size_t i = 0; i < moved_positions.size(); ++i) {
    if (correction_count[i] > 0) {
      moved_positions[i] = _Mul(correction_sum[i], 1.0f / static_cast<float>(correction_count[i]));
    }
    moved_positions[i] = _ClampToBoundary(moved_positions[i]);
  }

  output->positions = moved_positions;

  for (size_t i = 0; i < output->positions.size(); ++i) {
    if (displacement_target_set[i]) {
      output->positions[i] = displacement_targets[i];
    }
  }

  std::vector<Vec3> linear_force(output->positions.size(), Vec3{});
  std::vector<float> angular_force(output->positions.size(), 0.0f);

  for (const VelocityGuideForce& force_guide : output->guide_forces) {
    if (force_guide.hidden || !force_guide.valid) continue;
    for (int vertex : force_guide.vertices) {
      if (vertex < 0 || static_cast<size_t>(vertex) >= output->positions.size()) continue;
      linear_force[vertex] = _Add(linear_force[vertex], force_guide.force);
    }
  }

  for (size_t tri_index = 0; tri_index < input.rest_triangles.size(); ++tri_index) {
    const PhysicsRestTriangle& rest = input.rest_triangles[tri_index];
    if (rest.origin >= input.positions.size() || rest.first >= input.positions.size() || rest.second >= input.positions.size()) {
      continue;
    }

    const Vec3& p0 = output->positions[rest.origin];
    const Vec3& p1 = output->positions[rest.first];
    const Vec3& p2 = output->positions[rest.second];
    _Mat2 Ds = _MakeMat2(
      p1.x - p0.x,
      p2.x - p0.x,
      p1.y - p0.y,
      p2.y - p0.y);
    _Mat2 DmInv = _MakeMat2(rest.inv00, rest.inv01, rest.inv10, rest.inv11);
    _Mat2 F = _Mul(Ds, DmInv);
    float angle = _RotationAngleFromF(F);
    _Mat2 R = _MakeMat2(std::cos(angle), -std::sin(angle), std::sin(angle), std::cos(angle));

    float J = _Determinant(F);
    if (!std::isfinite(J)) J = 1.0f;
    if (std::fabs(J) < 1e-6f) J = J < 0.0f ? -1e-6f : 1e-6f;

    _Mat2 FinvT = _Transpose(_Inverse(F));
    float mu = _MaterialToMu(rest.material_gpa);
    float lambda = _MaterialToLam(rest.material_gpa);
    _Mat2 P = _Add(_Mul(_Sub(F, R), 2.0f * mu), _Mul(FinvT, lambda * (J - 1.0f) * J));
    _Mat2 H = _Mul(_Mul(P, _Transpose(DmInv)), -rest.rest_area);

    Vec2 f1{H.m00, H.m10};
    Vec2 f2{H.m01, H.m11};
    Vec2 f0{-(f1.x + f2.x), -(f1.y + f2.y)};

    Vec2 center{(p0.x + p1.x + p2.x) / 3.0f, (p0.y + p1.y + p2.y) / 3.0f};
    float torque = 0.0f;
    torque += _Cross2(Vec2{p0.x - center.x, p0.y - center.y}, f0);
    torque += _Cross2(Vec2{p1.x - center.x, p1.y - center.y}, f1);
    torque += _Cross2(Vec2{p2.x - center.x, p2.y - center.y}, f2);

    const uint32_t ids[3] = {rest.origin, rest.first, rest.second};
    const Vec2 forces[3] = {f0, f1, f2};
    for (int slot = 0; slot < 3; ++slot) {
      uint32_t id = ids[slot];
      if (id >= output->positions.size()) continue;
      linear_force[id] = _Add(linear_force[id], Vec3{forces[slot].x, forces[slot].y, 0.0f});
      angular_force[id] += torque / 3.0f;
    }
  }

  for (size_t i = 0; i < output->positions.size(); ++i) {
    Vec3 prev_linear_motion = ExtractLinearVelocity(output->linear_velocities[i]);
    float prev_angle = _RotationAngleFromMatrix(output->angular_velocities[i]);

    Vec3 next_linear_motion = _Add(
      _Mul(prev_linear_motion, kVelocityDecay),
      _Mul(linear_force[i], kDt * kDt / kMassPerVertex));
    float next_angle = prev_angle * kAngularVelocityDecay + angular_force[i] * (kDt * kDt / kInertiaPerVertex);
    next_angle = std::clamp(next_angle, -0.35f, 0.35f);

    output->linear_velocities[i] = MakeLinearVelocityMatrix(next_linear_motion);
    output->angular_velocities[i] = _MakeRotationVelocityMatrix(next_angle);
    output->total_velocities[i] = ComposeVelocity(output->angular_velocities[i], output->linear_velocities[i]);
  }
}

}  // namespace

bool CpuPhysicsAlgorithm_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  if (!result) return false;
  if (!_SupportsCpuAlgorithm(request.config.algorithm_name)) {
    return false;
  }
  if (request.input.rest_positions.size() < request.input.positions.size()) {
    return false;
  }

  result->step_output = _BuildInitialOutput(request.input);
  _RunLegacyCorotated(request.input, &result->step_output);
  result->executed = true;
  return true;
}

}  // namespace algorithm
