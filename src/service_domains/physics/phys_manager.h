#pragma once

#include "algorithm/algorithm_types.h"
#include "messaging/io_buffers.h"

#include "data_protocol/mesh.h"
#include "data_protocol/physics/physics_types.h"

#include <vector>

namespace service_domains {

class PhysManager {
 public:
  void SetConfig(const PhysSolverConfig& config);
  void SetGpuContext(const VulkanComputeContextView& context);
  void SetAlgorithmComplianceDescriptor(const AlgorithmComplianceDescriptor& descriptor);
  void Init(const Mesh& mesh);
  IoBufferEndpoint io_endpoint() { return IoBufferEndpoint{&inbound_io_packet_.protocol, &inbound_io_packet_.signal_buffer, &inbound_io_packet_.data_buffer}; }
  IoBufferPacket& inbound_io_packet() { return inbound_io_packet_; }
  const IoBufferPacket& inbound_io_packet() const { return inbound_io_packet_; }
  std::vector<IoSignalBufferEntry>& signal_buffer() { return inbound_io_packet_.signal_buffer; }
  std::vector<IoDataBufferEntry>& data_buffer() { return inbound_io_packet_.data_buffer; }
  const std::vector<IoSignalBufferEntry>& signal_buffer() const { return inbound_io_packet_.signal_buffer; }
  const std::vector<IoDataBufferEntry>& data_buffer() const { return inbound_io_packet_.data_buffer; }
  void ClearSubmissionBuffers();
  bool ResolveSignalDataBuffers(const Mesh& mesh);
  void SetVelocityGuidances(const Mesh& mesh, const std::vector<VelocityGuidance>& guidances);
  void SetGuideVelocities(const std::vector<VelocityGuideVelocity>& guide_velocities);
  void SetGuideForces(const std::vector<VelocityGuideForce>& guide_forces);
  void ApplyValidVelocityGuidances(Mesh& mesh, std::vector<VelocityMatrix>& total_velocities, std::vector<VelocityMatrix>& linear_velocities, std::vector<VelocityMatrix>& angular_velocities);

  PhysSolverKind solver_kind() const { return config_.solver_kind; }
  const std::string& algorithm_name() const { return config_.algorithm_name; }
  const AlgorithmComplianceDescriptor& algorithm_compliance_descriptor() const { return algorithm_compliance_descriptor_; }
  const std::vector<VelocityGuidance>& guidances() const { return guidances_; }
  const std::vector<VelocityGuideVelocity>& guide_velocities() const { return guide_velocities_; }
  const std::vector<VelocityGuideForce>& guide_forces() const { return guide_forces_; }
  const std::vector<VelocityGuidance>& decoded_velocity_guidances() const { return decoded_velocity_guidances_; }
  const std::vector<VelocityGuideVelocity>& decoded_guide_velocities() const { return decoded_guide_velocities_; }
  const std::vector<VelocityGuideForce>& decoded_guide_forces() const { return decoded_guide_forces_; }
  const GpuPhysicsDispatchDebugInfo& gpu_dispatch_debug_info() const { return gpu_dispatch_debug_info_; }

 private:
  void RevalidateGuidances(const Mesh& mesh);
  PhysicsAlgorithmRequest BuildRequest(
    const Mesh& mesh,
    const std::vector<VelocityMatrix>& total_velocities,
    const std::vector<VelocityMatrix>& linear_velocities,
    const std::vector<VelocityMatrix>& angular_velocities) const;
  void MaybeRunAlgorithmOnInit();

  bool initialized_{false};
  Mesh rest_mesh_{};
  std::vector<PhysicsRestTriangle> rest_triangles_;
  std::vector<VelocityGuidance> guidances_;
  std::vector<VelocityGuideVelocity> guide_velocities_;
  std::vector<VelocityGuideForce> guide_forces_;
  IoBufferPacket inbound_io_packet_{};
  std::vector<VelocityGuidance> decoded_velocity_guidances_;
  std::vector<VelocityGuideVelocity> decoded_guide_velocities_;
  std::vector<VelocityGuideForce> decoded_guide_forces_;
  PhysSolverConfig config_{};
  AlgorithmComplianceDescriptor algorithm_compliance_descriptor_{};
  VulkanComputeContextView compute_context_{};
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug_info_{};
};
}  // namespace service_domains
