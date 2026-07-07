#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/vk_job_system.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace runtime_systems {

struct RuntimeVkBufferBindingView {
  std::string binding_name;
  std::byte* bytes{nullptr};
  size_t size_bytes{0u};
  size_t element_stride{0u};
  bool array_like{false};
  bool required{true};
};

struct RuntimeVkStageSubJob {
  std::string debug_name;
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::vector<RuntimeVkBufferBindingView> buffer_bindings;
};

struct RuntimeVkStageJob {
  std::string debug_name;
  std::string shader_namespace;
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  float viewport_width{1.0f};
  float viewport_height{1.0f};
  const void* execution_key{nullptr};
  std::vector<RuntimeVkBufferBindingView> buffer_bindings;
  std::vector<RuntimeVkStageSubJob> stage_jobs;
};

void ClearRuntimeVkJobCaches();
bool ExecuteRuntimeVkJob(
  const RuntimeVkStageJob& job,
  std::string* out_error_message = nullptr);
bool SynchronizeRuntimeVkJob(
  const RuntimeVkStageJob& job,
  std::string* out_error_message = nullptr);

}  // namespace runtime_systems

