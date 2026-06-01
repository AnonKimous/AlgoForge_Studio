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

struct AlgorithmDataContract {
  std::vector<AlgorithmBufferRequirement> arrays_to_allocate;
  std::vector<AlgorithmBufferRequirement> temporary_registers_to_allocate;
  std::vector<AlgorithmBufferRequirement> temporary_caches_to_allocate;
  std::vector<AlgorithmDataFormat> filled_data_formats;
  std::vector<AlgorithmDataFormat> algorithm_required_formats;
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
  AlgorithmDataContract data_contract{};
};

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
using algorithm::AlgorithmDataContract;
using algorithm::AlgorithmDataFormat;
using algorithm::CreatePhysSolverInfo;
using algorithm::GpuPhysicsDispatchDebugInfo;
using algorithm::GpuPhysicsShaderSpec;
using algorithm::PhysSolverConfig;
using algorithm::PhysicsAlgorithmRequest;
using algorithm::PhysicsAlgorithmResult;
using algorithm::PhysSolverKind;
using algorithm::VulkanComputeContextView;
