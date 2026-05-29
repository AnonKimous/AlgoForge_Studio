#pragma once

#include "../physics/physics_types.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class PhysSolverKind {
  Cpu,
  Gpu,
};

struct GpuPhysicsShaderSpec {
  std::string shader_name;
  std::vector<uint32_t> shader_mask;
  std::vector<float> shader_data;
};

struct ReflectionMemoryRequest {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
};

struct ReflectionDataFormat {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
};

struct ReflectionMemoryBlock {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
  std::vector<std::byte> bytes;
};

struct DataReflectionCommit {
  std::vector<ReflectionMemoryBlock> arrays;
  std::vector<ReflectionMemoryBlock> temporary_registers;
  std::vector<ReflectionMemoryBlock> temporary_caches;
  std::vector<ReflectionDataFormat> filled_data_formats;
  std::vector<ReflectionDataFormat> algorithm_required_formats;
  bool valid{false};
};

struct CreateDataReflectionInfo;
using DataReflectionCallback = std::function<void(
  const CreateDataReflectionInfo& reflection_info,
  const std::vector<const void*>& real_data_addresses,
  DataReflectionCommit* reflection_commit)>;

struct CreateDataReflectionInfo {
  std::vector<ReflectionMemoryRequest> arrays_to_allocate;
  std::vector<ReflectionMemoryRequest> temporary_registers_to_allocate;
  std::vector<ReflectionMemoryRequest> temporary_caches_to_allocate;
  std::vector<ReflectionDataFormat> filled_data_formats;
  std::vector<ReflectionDataFormat> algorithm_required_formats;
  DataReflectionCallback reflection_callback;
};

struct CreatePhysSolverInfo {
  PhysSolverKind solver_kind{PhysSolverKind::Cpu};
  std::string algorithm_name;
  CreateDataReflectionInfo data_reflection_info{};
  std::vector<const void*> real_data_addresses;
  GpuPhysicsShaderSpec gpu_shader{};
  bool run_algorithm_on_init{false};
};

using PhysManagerConfig = CreatePhysSolverInfo;

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
  PhysManagerConfig config{};
  PhysicsStepInput input{};
  DataReflectionCommit reflection_commit{};
  VulkanComputeContextView compute_context{};
};

struct PhysicsAlgorithmResult {
  bool executed{false};
  PhysicsStepOutput step_output{};
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug{};
};

namespace data_protocol {
using ::CreateDataReflectionInfo;
using ::CreatePhysSolverInfo;
using ::DataReflectionCallback;
using ::DataReflectionCommit;
using ::GpuPhysicsDispatchDebugInfo;
using ::GpuPhysicsShaderSpec;
using ::PhysManagerConfig;
using ::PhysicsAlgorithmRequest;
using ::PhysicsAlgorithmResult;
using ::PhysSolverKind;
using ::ReflectionDataFormat;
using ::ReflectionMemoryBlock;
using ::ReflectionMemoryRequest;
using ::VulkanComputeContextView;
}  // namespace data_protocol
