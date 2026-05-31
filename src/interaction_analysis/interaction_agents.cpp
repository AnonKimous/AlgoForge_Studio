#include "interaction_agents.h"

#include "algorithm/physics_algorithm_pipeline.h"

#include <algorithm>
#include <cmath>

namespace interaction_analysis {

namespace {

bool IsMoreLowerLeft(const Vec3& candidate, const Vec3& current) {
  float candidate_score = candidate.x + candidate.y;
  float current_score = current.x + current.y;
  if (candidate_score != current_score) return candidate_score < current_score;
  if (candidate.y != current.y) return candidate.y < current.y;
  return candidate.x < current.x;
}

float ClampMaterialGpa(float material_gpa) {
  if (!(std::isfinite(material_gpa) && material_gpa > 0.0f)) {
    return 1000000.0f;
  }
  return material_gpa;
}

std::vector<PhysicsRestTriangle> BuildRestTriangles(const Mesh& mesh) {
  std::vector<PhysicsRestTriangle> rest_triangles;
  rest_triangles.reserve(mesh.triangles.size());

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
    rest_triangles.push_back(rest);
  }

  return rest_triangles;
}

}  // namespace

void PhysAgent::Init(
  const PhysSolverConfig& config,
  const VulkanComputeContextView& compute_context,
  const AlgorithmComplianceDescriptor& compliance_descriptor) {
  config_ = config;
  compute_context_ = compute_context;
  compliance_descriptor_ = compliance_descriptor;
  run_state_ = PhysRunState::Pause;
  algorithm_intervention_ = AlgorithmInterventionDescriptor{};
  initialized_ = false;
}

void PhysAgent::EnsureInitialized(const Mesh& mesh) {
  if (initialized_) return;

  rest_mesh_ = mesh;
  rest_triangles_ = BuildRestTriangles(mesh);
  initial_positions_ = mesh.positions;
  total_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  linear_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  angular_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  initialized_ = true;
}

void PhysAgent::ApplyAlgorithmIntervention(const AlgorithmInterventionDescriptor& intervention, Mesh& mesh) {
  (void)mesh;
  CodecManager codec{};
  IoBufferPacket packet = codec.BuildAlgorithmInterventionPacket(intervention);
  AlgorithmInterventionDescriptor decoded{};
  if (codec.DecodeAlgorithmInterventionPacket(packet, &decoded)) {
    algorithm_intervention_ = decoded;
  }
}

PhysicsAlgorithmRequest PhysAgent::BuildRequest(const Mesh& mesh) const {
  PhysicsAlgorithmRequest request{};
  request.config = config_;
  request.compute_context = compute_context_;
  request.input.positions = mesh.positions;
  request.input.rest_positions = rest_mesh_.positions;
  request.input.total_velocities = total_velocities_;
  request.input.linear_velocities = linear_velocities_;
  request.input.angular_velocities = angular_velocities_;
  request.input.rest_triangles = rest_triangles_;
  request.input.displacement_interventions = active_displacement_interventions_;
  request.input.velocity_interventions = active_velocity_interventions_;
  request.input.force_interventions = active_force_interventions_;
  if (request.input.total_velocities.size() < request.input.positions.size()) {
    request.input.total_velocities.resize(request.input.positions.size(), MakeIdentityVelocity());
  }
  if (request.input.linear_velocities.size() < request.input.positions.size()) {
    request.input.linear_velocities.resize(request.input.positions.size(), MakeIdentityVelocity());
  }
  if (request.input.angular_velocities.size() < request.input.positions.size()) {
    request.input.angular_velocities.resize(request.input.positions.size(), MakeIdentityVelocity());
  }
  for (size_t i = 0; i < request.input.rest_triangles.size(); ++i) {
    if (i < mesh.triangle_material_gpa.size()) {
      request.input.rest_triangles[i].material_gpa = ClampMaterialGpa(mesh.triangle_material_gpa[i]);
    }
  }
  return request;
}

void PhysAgent::AdvancePhysicsStep(Mesh& mesh) {
  EnsureInitialized(mesh);
  PhysicsAlgorithmResult result{};
  PhysicsAlgorithmRequest request = BuildRequest(mesh);
  if (!PhysicsAlgorithmPipeline_Run(request, &result) || !result.executed) {
    return;
  }
  mesh.positions = std::move(result.step_output.positions);
  total_velocities_ = std::move(result.step_output.total_velocities);
  linear_velocities_ = std::move(result.step_output.linear_velocities);
  angular_velocities_ = std::move(result.step_output.angular_velocities);
  active_displacement_interventions_ = std::move(result.step_output.displacement_interventions);
  active_velocity_interventions_ = std::move(result.step_output.velocity_interventions);
  active_force_interventions_ = std::move(result.step_output.force_interventions);
  if (result.gpu_dispatch_debug.valid) {
    gpu_dispatch_debug_info_ = std::move(result.gpu_dispatch_debug);
  }
  ++frame_index_;
}

void PhysAgent::Tick(
  Mesh& mesh,
  const ViewportTransform& viewport,
  const SceneCamera& camera,
  const InputState& input,
  Vec2 mouse_pixel,
  float dt_seconds) {
  (void)viewport;
  (void)camera;
  (void)input;
  (void)mouse_pixel;
  EnsureInitialized(mesh);
  if (run_state_ == PhysRunState::Run) {
    physics_accumulator_ += std::max(dt_seconds, 0.0f);
    constexpr float kPhysicsStepSeconds = 1.0f / 120.0f;
    while (physics_accumulator_ >= kPhysicsStepSeconds) {
      AdvancePhysicsStep(mesh);
      physics_accumulator_ -= kPhysicsStepSeconds;
    }
  } else {
    physics_accumulator_ = 0.0f;
  }
}

void PhysAgent::Destroy() {
  initialized_ = false;
  run_state_ = PhysRunState::Pause;
  physics_accumulator_ = 0.0f;
  frame_index_ = 0;
  rest_mesh_ = Mesh{};
  rest_triangles_.clear();
  initial_positions_.clear();
  total_velocities_.clear();
  linear_velocities_.clear();
  angular_velocities_.clear();
  active_displacement_interventions_.clear();
  active_velocity_interventions_.clear();
  active_force_interventions_.clear();
  algorithm_intervention_ = AlgorithmInterventionDescriptor{};
  gpu_dispatch_debug_info_ = GpuPhysicsDispatchDebugInfo{};
}

void PhysAgent::StepOnce(Mesh& mesh) {
  EnsureInitialized(mesh);
  if (run_state_ != PhysRunState::Pause) return;
  physics_accumulator_ = 0.0f;
  AdvancePhysicsStep(mesh);
}

void PhysAgent::Reset(Mesh& mesh) {
  EnsureInitialized(mesh);
  if (initial_positions_.size() == mesh.positions.size()) {
    mesh.positions = initial_positions_;
  }
  total_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  linear_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  angular_velocities_.assign(mesh.positions.size(), MakeIdentityVelocity());
  active_displacement_interventions_.clear();
  active_velocity_interventions_.clear();
  active_force_interventions_.clear();
  frame_index_ = 0;
  physics_accumulator_ = 0.0f;
  run_state_ = PhysRunState::Pause;
}

}  // namespace interaction_analysis
