#include "random_vertex_motion_algorithm_contract.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace algorithm_library {

namespace {

uint32_t Hash32(uint32_t value) {
  value ^= value >> 17;
  value *= 0xed5ad4bbU;
  value ^= value >> 11;
  value *= 0xac4c1b51U;
  value ^= value >> 15;
  value *= 0x31848babU;
  value ^= value >> 14;
  return value;
}

float HashToUnit(uint32_t value) {
  return static_cast<float>(Hash32(value)) / static_cast<float>(std::numeric_limits<uint32_t>::max());
}

Vec3 MakeRandomDirection(uint32_t seed) {
  const float x = HashToUnit(seed ^ 0x9e3779b9U) * 2.0f - 1.0f;
  const float y = HashToUnit(seed ^ 0x7f4a7c15U) * 2.0f - 1.0f;
  const float z = HashToUnit(seed ^ 0x94d049bbU) * 2.0f - 1.0f;
  const float length = std::sqrt(x * x + y * y + z * z);
  if (length <= 1e-6f) {
    return Vec3{1.0f, 0.0f, 0.0f};
  }
  return Vec3{x / length, y / length, z / length};
}

Vec3 Add(Vec3 a, Vec3 b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Sub(Vec3 a, Vec3 b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Mul(Vec3 value, float scale) {
  return Vec3{value.x * scale, value.y * scale, value.z * scale};
}

float Length(Vec3 value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 ClampToRadius(Vec3 center, Vec3 position, float radius) {
  Vec3 delta = Sub(position, center);
  const float distance = Length(delta);
  if (distance <= radius || distance <= 1e-6f) {
    return position;
  }
  return Add(center, Mul(delta, radius / distance));
}

}  // namespace

bool RandomVertexMotionAlgorithm_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  if (!result) return false;
  *result = PhysicsAlgorithmResult{};

  if (request.config.algorithm_name != kRandomVertexMotionAlgorithmName) {
    return false;
  }
  if (request.input.positions.empty() || request.input.rest_positions.size() < request.input.positions.size()) {
    return false;
  }

  const float requested_radius = request.intervention_request.enabled && request.intervention_request.radius > 0.0f
    ? request.intervention_request.radius
    : request.compliance_descriptor.motion_radius;
  const float motion_radius = std::max(0.0001f, requested_radius);
  const float jitter_strength = motion_radius * 0.12f;
  const float drag = 0.88f;

  result->executed = true;
  result->step_output.positions = request.input.positions;
  result->step_output.total_velocities = request.input.total_velocities;
  result->step_output.linear_velocities = request.input.linear_velocities;
  result->step_output.angular_velocities = request.input.angular_velocities;
  result->step_output.displacement_interventions = request.input.displacement_interventions;
  result->step_output.velocity_interventions = request.input.velocity_interventions;
  result->step_output.force_interventions = request.input.force_interventions;
  if (result->step_output.total_velocities.size() < result->step_output.positions.size()) {
    result->step_output.total_velocities.resize(result->step_output.positions.size(), MakeIdentityVelocity());
  }
  if (result->step_output.linear_velocities.size() < result->step_output.positions.size()) {
    result->step_output.linear_velocities.resize(result->step_output.positions.size(), MakeIdentityVelocity());
  }
  if (result->step_output.angular_velocities.size() < result->step_output.positions.size()) {
    result->step_output.angular_velocities.resize(result->step_output.positions.size(), MakeIdentityVelocity());
  }

  for (size_t index = 0; index < request.input.positions.size(); ++index) {
    const Vec3 center = request.input.rest_positions[index];
    const Vec3 current = request.input.positions[index];
    const Vec3 previous_velocity = ExtractLinearVelocity(request.input.linear_velocities[index]);
    const uint32_t seed = Hash32(static_cast<uint32_t>(index) ^ static_cast<uint32_t>(std::lround(center.x * 1000.0f)) ^
      static_cast<uint32_t>(std::lround(center.y * 1000.0f)) ^ static_cast<uint32_t>(std::lround(center.z * 1000.0f)));
    const Vec3 random_direction = MakeRandomDirection(seed);
    Vec3 next_velocity = Add(Mul(previous_velocity, drag), Mul(random_direction, jitter_strength));
    Vec3 next_position = Add(current, next_velocity);
    next_position = ClampToRadius(center, next_position, motion_radius);
    const Vec3 adjusted_delta = Sub(next_position, current);
    next_velocity = Add(Mul(previous_velocity, drag), adjusted_delta);

    result->step_output.positions[index] = next_position;
    result->step_output.linear_velocities[index] = MakeLinearVelocityMatrix(next_velocity);
    result->step_output.angular_velocities[index] = MakeIdentityVelocity();
    result->step_output.total_velocities[index] = ComposeVelocity(
      result->step_output.angular_velocities[index],
      result->step_output.linear_velocities[index]);
  }

  result->algorithm_to_agent_signal.intervention_needed =
    request.agent_to_algorithm_signal.needs_intervention || request.intervention_request.enabled;
  result->algorithm_to_agent_signal.pause_requested = request.agent_to_algorithm_signal.pause_requested;
  result->algorithm_to_agent_signal.stop_requested = request.agent_to_algorithm_signal.stop_requested;
  return true;
}

}  // namespace algorithm_library
