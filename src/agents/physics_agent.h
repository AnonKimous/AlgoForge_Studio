#pragma once

#include "agents.h"
#include "algorithm_library/algorithm_types.h"
#include "common_data/interaction/interaction_state.h"
#include "common_data/mesh.h"
#include "common_data/input_state.h"
#include <string>
#include <vector>

namespace agent_execute {

class PhysicsAgent {
 public:
  void Init(
    const PhysSolverConfig& config,
    const VulkanComputeContextView& compute_context,
    const AlgorithmContainerDescriptor& container_descriptor = {});
  void Tick(
    Mesh& mesh,
    const InputState& input,
    Vec2 mouse_pixel,
    float dt_seconds);
  void Destroy();

  void SetRunState(PhysRunState run_state) { run_state_ = run_state; }
  void SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package) { algorithm_runtime_.SetInterventionPackage(std::move(package)); }
  void ApplyInterventionRequest(const InteractionInterventionRequest& request);
  void SetAgentToAlgorithmSignal(const AgentToAlgorithmSignal& signal) { algorithm_runtime_.SetAgentToAlgorithmSignal(signal); }

  const AgentToAlgorithmSignal& agent_to_algorithm_signal() const { return algorithm_runtime_.agent_to_algorithm_signal(); }
  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return algorithm_runtime_.algorithm_to_agent_signal(); }
  const InteractionInterventionRequest& intervention_request() const { return algorithm_runtime_.intervention_request(); }

  void StepOnce(Mesh& mesh);
  void Reset(Mesh& mesh);

 private:
  void EnsureInitialized(const Mesh& mesh);
  void AdvancePhysicsStep(Mesh& mesh);
  PhysicsAlgorithmRequest BuildRequest(const Mesh& mesh) const;
  void CaptureResultFeedback(const PhysicsAlgorithmResult& result);

  bool initialized_{false};
  AgentAlgorithmRuntime algorithm_runtime_{};
  PhysRunState run_state_{PhysRunState::Pause};
  float physics_accumulator_{0.0f};
  Mesh rest_mesh_{};
  std::vector<PhysicsRestTriangle> rest_triangles_;
  std::vector<Vec3> initial_positions_;
  std::vector<VelocityMatrix> total_velocities_;
  std::vector<VelocityMatrix> linear_velocities_;
  std::vector<VelocityMatrix> angular_velocities_;
  std::vector<InterventionDisplacement> active_displacement_interventions_;
  std::vector<InterventionVelocity> active_velocity_interventions_;
  std::vector<InterventionForce> active_force_interventions_;
};

using PhysAgent = PhysicsAgent;

}  // namespace agent_execute

using agent_execute::PhysAgent;
using agent_execute::PhysicsAgent;
