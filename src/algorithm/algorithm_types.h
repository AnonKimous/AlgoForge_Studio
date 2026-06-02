#pragma once

#include "common_data/interaction/interaction_signals.h"
#include "common_data/physics/physics_types.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace algorithm {

enum class PhysSolverKind {
  Cpu,
  Gpu,
};

struct GpuPhysicsShaderSpec {
  std::string shader_name;
  std::vector<uint32_t> shader_mask;
  std::vector<float> shader_data;
};

struct AlgorithmBufferRequirement {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
};

struct AlgorithmDataFormat {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
};

struct AlgorithmContainerAlias {
  std::string package_name;
  std::string source_name;
  std::string alias_name;
};

struct AlgorithmDataContract {
  std::vector<AlgorithmBufferRequirement> arrays_to_allocate;
  std::vector<AlgorithmBufferRequirement> temporary_registers_to_allocate;
  std::vector<AlgorithmBufferRequirement> temporary_caches_to_allocate;
  std::vector<AlgorithmDataFormat> filled_data_formats;
  std::vector<AlgorithmDataFormat> algorithm_required_formats;
  std::vector<AlgorithmContainerAlias> container_aliases;
};

struct CreatePhysSolverInfo {
  PhysSolverKind solver_kind{PhysSolverKind::Cpu};
  std::string algorithm_name;
  GpuPhysicsShaderSpec gpu_shader{};
  bool run_algorithm_on_init{false};
};

using PhysSolverConfig = CreatePhysSolverInfo;

struct AlgorithmComplianceDescriptor {
  std::string algorithm_name;
  bool cpu_available{true};
  bool gpu_available{false};
  float motion_radius{0.0f};
  AlgorithmDataContract data_contract{};
};

using AlgorithmContainerDescriptor = AlgorithmComplianceDescriptor;

inline const AlgorithmContainerAlias* FindAlgorithmContainerAlias(
  const AlgorithmDataContract& data_contract,
  const std::string& package_name,
  const std::string& source_name) {
  for (const auto& alias : data_contract.container_aliases) {
    if ((alias.package_name.empty() || alias.package_name == package_name) && alias.source_name == source_name) {
      return &alias;
    }
  }
  return nullptr;
}

inline std::string ResolveAlgorithmContainerName(
  const AlgorithmDataContract& data_contract,
  const std::string& package_name,
  const std::string& source_name) {
  const AlgorithmContainerAlias* alias = FindAlgorithmContainerAlias(data_contract, package_name, source_name);
  return alias ? alias->alias_name : source_name;
}

inline std::string ResolveAlgorithmContainerName(
  const AlgorithmComplianceDescriptor& compliance_descriptor,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(compliance_descriptor.data_contract, package_name, source_name);
}

struct VulkanComputeContextView {
  VkInstance instance{};
  VkPhysicalDevice physical_device{};
  VkDevice device{};
  VkQueue queue{};
  uint32_t queue_family_index{};
  bool valid{false};
};

struct GpuPhysicsDispatchDebugInfo {
  std::string shader_name;
  uint32_t width{};
  uint32_t height{};
  std::vector<float> values;
  bool valid{false};
};

struct PhysicsAlgorithmRequest {
  PhysSolverConfig config{};
  PhysicsStepInput input{};
  VulkanComputeContextView compute_context{};
  AlgorithmComplianceDescriptor compliance_descriptor{};
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  InteractionInterventionRequest intervention_request{};
};

struct PhysicsAlgorithmResult {
  bool executed{false};
  PhysicsStepOutput step_output{};
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
};

}  // namespace algorithm

using algorithm::AlgorithmBufferRequirement;
using algorithm::AlgorithmComplianceDescriptor;
using algorithm::AlgorithmContainerDescriptor;
using algorithm::AlgorithmDataContract;
using algorithm::AlgorithmDataFormat;
using algorithm::AlgorithmContainerAlias;
using algorithm::CreatePhysSolverInfo;
using algorithm::GpuPhysicsDispatchDebugInfo;
using algorithm::GpuPhysicsShaderSpec;
using algorithm::PhysSolverConfig;
using algorithm::PhysicsAlgorithmRequest;
using algorithm::PhysicsAlgorithmResult;
using algorithm::PhysSolverKind;
using algorithm::VulkanComputeContextView;
