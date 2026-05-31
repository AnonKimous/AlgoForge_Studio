#pragma once

#include "algorithm/algorithm_types.h"
#include "codec/codec_manager.h"
#include "common_data/interaction/interaction_state.h"
#include "common_data/mesh.h"
#include "common_data/input_state.h"
#include "common_data/viewport_transform.h"
#include "service_domains/render/scene_camera.h"

#include <string>
#include <vector>

namespace interaction_analysis {

class PhysAgent {
 public:
  void Init(
    const PhysSolverConfig& config,
    const VulkanComputeContextView& compute_context,
    const AlgorithmComplianceDescriptor& compliance_descriptor = {});
  void Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  void SetRunState(PhysRunState run_state) { run_state_ = run_state; }
  void ApplyAlgorithmIntervention(const AlgorithmInterventionDescriptor& intervention, Mesh& mesh);
  PhysRunState run_state() const { return run_state_; }
  const std::vector<VelocityMatrix>& total_velocities() const { return total_velocities_; }
  const std::vector<VelocityMatrix>& linear_velocities() const { return linear_velocities_; }
  const std::vector<VelocityMatrix>& angular_velocities() const { return angular_velocities_; }
  int current_frame_index() const { return static_cast<int>(frame_index_); }
  PhysSolverKind solver_kind() const { return config_.solver_kind; }
  const std::string& algorithm_name() const { return config_.algorithm_name; }
  const GpuPhysicsDispatchDebugInfo& gpu_dispatch_debug_info() const { return gpu_dispatch_debug_info_; }

  void StepOnce(Mesh& mesh);
  void Reset(Mesh& mesh);

 private:
  void EnsureInitialized(const Mesh& mesh);
  void AdvancePhysicsStep(Mesh& mesh);
  PhysicsAlgorithmRequest BuildRequest(const Mesh& mesh) const;

  bool initialized_{false};
  PhysSolverConfig config_{};
  VulkanComputeContextView compute_context_{};
  AlgorithmComplianceDescriptor compliance_descriptor_{};
  PhysRunState run_state_{PhysRunState::Pause};
  float physics_accumulator_{0.0f};
  uint64_t frame_index_{0};
  Mesh rest_mesh_{};
  std::vector<PhysicsRestTriangle> rest_triangles_;
  std::vector<Vec3> initial_positions_;
  std::vector<VelocityMatrix> total_velocities_;
  std::vector<VelocityMatrix> linear_velocities_;
  std::vector<VelocityMatrix> angular_velocities_;
  std::vector<InterventionDisplacement> active_displacement_interventions_;
  std::vector<InterventionVelocity> active_velocity_interventions_;
  std::vector<InterventionForce> active_force_interventions_;
  AlgorithmInterventionDescriptor algorithm_intervention_{};
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug_info_{};
};

}  // namespace interaction_analysis
