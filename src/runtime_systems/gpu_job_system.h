#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/gpu_job_system.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace runtime_systems {

struct RuntimeGpuBufferBindingView {
  std::string binding_name;
  std::byte* bytes{nullptr};
  size_t size_bytes{0u};
  size_t element_stride{0u};
  bool required{true};
};

struct RuntimeGpuStageJob {
  std::string debug_name;
  std::string shader_namespace;
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  const void* execution_key{nullptr};
  std::vector<RuntimeGpuBufferBindingView> buffer_bindings;
};

void ClearRuntimeGpuJobCaches();
bool ExecuteRuntimeGpuJob(
  const RuntimeGpuStageJob& job,
  std::string* out_error_message = nullptr);
bool SynchronizeRuntimeGpuJob(
  const RuntimeGpuStageJob& job,
  std::string* out_error_message = nullptr);

}  // namespace runtime_systems
