#include "phys_manager.h"

#include "decomposition/decomposition_manager.h"
#include "algorithm/physics_algorithm_pipeline.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

bool _IsMoreLowerLeft(const Vec3& candidate, const Vec3& current) {
  float candidate_score = candidate.x + candidate.y;
  float current_score = current.x + current.y;
  if (candidate_score != current_score) return candidate_score < current_score;
  if (candidate.y != current.y) return candidate.y < current.y;
  return candidate.x < current.x;
}

float _ClampMaterialGpa(float material_gpa) {
  if (!(std::isfinite(material_gpa) && material_gpa > 0.0f)) {
    return 1000000.0f;
  }
  return material_gpa;
}

std::vector<PhysicsRestTriangle> _BuildRestTriangles(const Mesh& mesh) {
  std::vector<PhysicsRestTriangle> rest_triangles;
  rest_triangles.reserve(mesh.triangles.size());

  for (uint32_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
    const auto& tri = mesh.triangles[tri_index];
    uint32_t origin_slot = 0;
    for (uint32_t slot = 1; slot < 3; ++slot) {
      if (_IsMoreLowerLeft(mesh.positions[tri[slot]], mesh.positions[tri[origin_slot]])) {
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
      rest.material_gpa = _ClampMaterialGpa(mesh.triangle_material_gpa[tri_index]);
    }
    rest_triangles.push_back(rest);
  }

  return rest_triangles;
}

}  // namespace

namespace service_domains {

void PhysManager::SetConfig(const PhysSolverConfig& config) {
  config_ = config;
  if (initialized_) {
    MaybeRunAlgorithmOnInit();
  }
}

void PhysManager::SetGpuContext(const VulkanComputeContextView& context) {
  compute_context_ = context;
  if (initialized_) {
    MaybeRunAlgorithmOnInit();
  }
}

void PhysManager::SetAlgorithmComplianceDescriptor(const AlgorithmComplianceDescriptor& descriptor) {
  algorithm_compliance_descriptor_ = descriptor;
}

void PhysManager::Init(const Mesh& mesh) {
  rest_mesh_ = mesh;
  rest_triangles_ = _BuildRestTriangles(mesh);
  initialized_ = true;
  RevalidateGuidances(mesh);
  MaybeRunAlgorithmOnInit();
}

void PhysManager::ClearSubmissionBuffers() {
  inbound_io_packet_.protocol = IoProtocolDescriptor{};
  inbound_io_packet_.signal_buffer.clear();
  inbound_io_packet_.data_buffer.clear();
  decoded_velocity_guidances_.clear();
  decoded_guide_velocities_.clear();
  decoded_guide_forces_.clear();
}

bool PhysManager::ResolveSignalDataBuffers(const Mesh& mesh) {
  (void)mesh;
  decoded_velocity_guidances_.clear();
  decoded_guide_velocities_.clear();
  decoded_guide_forces_.clear();

  decomposition::DecomposedGuideBuffers decomposed{};
  decomposition::CodecManager decomposer{};
  if (decomposer.DecodeGuideUiPhysPacket(inbound_io_packet_, &decomposed)) {
    decoded_velocity_guidances_ = std::move(decomposed.guidances);
    decoded_guide_velocities_ = std::move(decomposed.guide_velocities);
    decoded_guide_forces_ = std::move(decomposed.guide_forces);
    return true;
  }
  return false;
}

void PhysManager::SetVelocityGuidances(const Mesh& mesh, const std::vector<VelocityGuidance>& guidances) {
  guidances_ = guidances;
  RevalidateGuidances(mesh);
}

void PhysManager::SetGuideVelocities(const std::vector<VelocityGuideVelocity>& guide_velocities) {
  guide_velocities_ = guide_velocities;
}

void PhysManager::SetGuideForces(const std::vector<VelocityGuideForce>& guide_forces) {
  guide_forces_ = guide_forces;
}

void PhysManager::ApplyValidVelocityGuidances(Mesh& mesh, std::vector<VelocityMatrix>& total_velocities, std::vector<VelocityMatrix>& linear_velocities, std::vector<VelocityMatrix>& angular_velocities) {
  if (!initialized_) {
    Init(mesh);
  }

  PhysicsAlgorithmRequest request = BuildRequest(mesh, total_velocities, linear_velocities, angular_velocities);
  PhysicsAlgorithmResult result{};
  if (!PhysicsAlgorithmPipeline_Run(request, &result) || !result.executed) {
    return;
  }

  mesh.positions = std::move(result.step_output.positions);
  total_velocities = std::move(result.step_output.total_velocities);
  linear_velocities = std::move(result.step_output.linear_velocities);
  angular_velocities = std::move(result.step_output.angular_velocities);
  guidances_ = std::move(result.step_output.guidances);
  guide_velocities_ = std::move(result.step_output.guide_velocities);
  guide_forces_ = std::move(result.step_output.guide_forces);
  if (result.gpu_dispatch_debug.valid) {
    gpu_dispatch_debug_info_ = std::move(result.gpu_dispatch_debug);
  }
}

void PhysManager::RevalidateGuidances(const Mesh& mesh) {
  if (!initialized_) return;
  if (config_.solver_kind != PhysSolverKind::Cpu) {
    return;
  }

  std::vector<VelocityMatrix> neutral_total(mesh.positions.size(), MakeIdentityVelocity());
  std::vector<VelocityMatrix> neutral_linear(mesh.positions.size(), MakeIdentityVelocity());
  std::vector<VelocityMatrix> neutral_angular(mesh.positions.size(), MakeIdentityVelocity());
  PhysicsAlgorithmRequest request = BuildRequest(mesh, neutral_total, neutral_linear, neutral_angular);
  PhysicsAlgorithmResult result{};
  if (!PhysicsAlgorithmPipeline_Run(request, &result) || !result.executed) {
    return;
  }
  guidances_ = std::move(result.step_output.guidances);
}

PhysicsAlgorithmRequest PhysManager::BuildRequest(
  const Mesh& mesh,
  const std::vector<VelocityMatrix>& total_velocities,
  const std::vector<VelocityMatrix>& linear_velocities,
  const std::vector<VelocityMatrix>& angular_velocities) const {
  PhysicsAlgorithmRequest request{};
  request.config = config_;
  request.compute_context = compute_context_;
  request.input.positions = mesh.positions;
  request.input.rest_positions = rest_mesh_.positions;
  request.input.total_velocities = total_velocities;
  request.input.linear_velocities = linear_velocities;
  request.input.angular_velocities = angular_velocities;
  request.input.rest_triangles = rest_triangles_;
  request.input.guidances = guidances_;
  request.input.guide_velocities = guide_velocities_;
  request.input.guide_forces = guide_forces_;
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
      request.input.rest_triangles[i].material_gpa = _ClampMaterialGpa(mesh.triangle_material_gpa[i]);
    }
  }
  return request;
}

void PhysManager::MaybeRunAlgorithmOnInit() {
  if (!initialized_ || !config_.run_algorithm_on_init) return;

  std::vector<VelocityMatrix> neutral_total(rest_mesh_.positions.size(), MakeIdentityVelocity());
  std::vector<VelocityMatrix> neutral_linear(rest_mesh_.positions.size(), MakeIdentityVelocity());
  std::vector<VelocityMatrix> neutral_angular(rest_mesh_.positions.size(), MakeIdentityVelocity());
  PhysicsAlgorithmRequest request = BuildRequest(rest_mesh_, neutral_total, neutral_linear, neutral_angular);
  PhysicsAlgorithmResult result{};
  if (!PhysicsAlgorithmPipeline_Run(request, &result) || !result.executed) {
    return;
  }
  if (result.gpu_dispatch_debug.valid) {
    gpu_dispatch_debug_info_ = std::move(result.gpu_dispatch_debug);
  }
}

}  // namespace service_domains
