#include "physics_solver.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPhysicsFps = 120.0f;
constexpr float kDt = 1.0f / kPhysicsFps;
constexpr float kTensionRate = 18.0f;
constexpr float kTensionAlpha = kTensionRate * kDt;
constexpr int kTensionIterations = 4;
constexpr float kEpsilon = 1e-5f;

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

}  // namespace

void PhysicsSolver::Init(const Mesh& mesh) {
  initial_mesh_ = mesh;
  rest_matrices_.clear();
  rest_matrices_.reserve(mesh.triangles.size());
  directives_.clear();

  for (const auto& tri : mesh.triangles) {
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
  directive.valid = true;

  for (PhysDirective& existing : directives_) {
    if (existing.vertex == vertex) {
      existing = directive;
      RevalidateDirectives(mesh);
      return;
    }
  }

  directives_.push_back(directive);
  RevalidateDirectives(mesh);
}

void PhysicsSolver::ApplyValidDirectives(Mesh& mesh) {
  PhysicsStepOutput output = Solve(BuildStepInput(mesh));
  mesh.positions = output.positions;
  directives_ = output.directives;
}

void PhysicsSolver::RevalidateDirectives(const Mesh& mesh) {
  PhysicsStepOutput output = Solve(BuildStepInput(mesh));
  directives_ = output.directives;
}

PhysicsStepInput PhysicsSolver::BuildStepInput(const Mesh& mesh) const {
  PhysicsStepInput input{};
  input.positions = mesh.positions;
  input.rest_triangles = rest_matrices_;
  input.directives = directives_;
  return input;
}

PhysicsStepOutput PhysicsSolver::Solve(const PhysicsStepInput& input) const {
  PhysicsStepOutput output{};
  output.positions = input.positions;
  output.directives = input.directives;

  for (PhysDirective& directive : output.directives) {
    if (directive.vertex < 0 || static_cast<size_t>(directive.vertex) >= output.positions.size()) {
      directive.valid = false;
      continue;
    }

    Vec3 current = output.positions[directive.vertex];
    output.positions[directive.vertex] = directive.requested_target;
    if (IsMeshStateAllowed(output.positions, input.rest_triangles)) {
      directive.allowed_target = directive.requested_target;
      directive.valid = true;
      continue;
    }

    output.positions[directive.vertex] = current;
    directive.allowed_target = FindNearestAllowedTarget(output.positions, input.rest_triangles, directive.vertex, current, directive.requested_target);
    output.positions[directive.vertex] = directive.allowed_target;
    bool allowed = IsMeshStateAllowed(output.positions, input.rest_triangles);
    directive.valid = false;
    if (allowed && Length2(Sub(directive.allowed_target, current)) > kEpsilon * kEpsilon) {
      continue;
    } else {
      output.positions[directive.vertex] = current;
    }
  }

  PropagateTriangleTension(input, output);
  return output;
}

bool PhysicsSolver::IsMeshStateAllowed(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles) const {
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
  return strain_norm <= 0.85f;
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

void PhysicsSolver::PropagateTriangleTension(const PhysicsStepInput& input, PhysicsStepOutput& output) const {
  if (input.positions.size() != output.positions.size()) return;

  std::vector<bool> guided(output.positions.size(), false);
  for (const PhysDirective& directive : output.directives) {
    if (directive.vertex >= 0 && static_cast<size_t>(directive.vertex) < guided.size()) {
      guided[directive.vertex] = Length2(Sub(output.positions[directive.vertex], input.positions[directive.vertex])) > kEpsilon * kEpsilon;
    }
  }

  for (int iteration = 0; iteration < kTensionIterations; ++iteration) {
    std::vector<Vec3> accumulated(output.positions.size());
    std::vector<uint32_t> counts(output.positions.size(), 0);

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
      triangle_pull = Mul(triangle_pull, kTensionAlpha);
      for (uint32_t id : ids) {
        if (guided[id]) continue;
        accumulated[id] = Add(accumulated[id], triangle_pull);
        ++counts[id];
      }
    }

    std::vector<Vec3> trial = output.positions;
    for (uint32_t i = 0; i < trial.size(); ++i) {
      if (counts[i] == 0) continue;
      // Multiple triangles pull the same vertex by vector sum; clamp only by dt scale above.
      trial[i] = Add(trial[i], accumulated[i]);
    }

    if (IsMeshStateAllowed(trial, input.rest_triangles)) {
      output.positions = trial;
      continue;
    }

    for (uint32_t i = 0; i < output.positions.size(); ++i) {
      if (counts[i] == 0) continue;
      Vec3 original = output.positions[i];
      Vec3 requested = Add(original, accumulated[i]);
      Vec3 allowed = FindNearestAllowedTarget(output.positions, input.rest_triangles, static_cast<int>(i), original, requested);
      output.positions[i] = allowed;
      if (!IsMeshStateAllowed(output.positions, input.rest_triangles)) {
        output.positions[i] = original;
      }
    }
  }
}
