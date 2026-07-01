#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/gpu_job_system.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace agent {
struct AlgorithmObject;
struct AgentTickContext;
}  // namespace agent

namespace algorithm {
struct AlgorithmContainerSet;
}  // namespace algorithm

namespace runtime_systems {

struct RuntimeGpuBufferBindingView {
  std::string binding_name;
  std::byte* bytes{nullptr};
  size_t size_bytes{0u};
  size_t element_stride{0u};
  bool array_like{false};
  bool required{true};
};

struct RuntimeGpuStageSubJob {
  std::string debug_name;
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::vector<RuntimeGpuBufferBindingView> buffer_bindings;
};

struct RuntimeGpuStageJob {
  std::string debug_name;
  std::string shader_namespace;
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  float viewport_width{1.0f};
  float viewport_height{1.0f};
  const void* execution_key{nullptr};
  std::vector<RuntimeGpuBufferBindingView> buffer_bindings;
  std::vector<RuntimeGpuStageSubJob> stage_jobs;
};

void ClearRuntimeGpuJobCaches();
bool ExecuteRuntimeGpuJob(
  const RuntimeGpuStageJob& job,
  std::string* out_error_message = nullptr);
bool SynchronizeRuntimeGpuJob(
  const RuntimeGpuStageJob& job,
  std::string* out_error_message = nullptr);
bool HasExecutableRuntimeGpuAlgorithmStage(const ::agent::AlgorithmObject& object);
bool ExecuteRuntimeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr);
bool SynchronizeRuntimeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr);

}  // namespace runtime_systems
