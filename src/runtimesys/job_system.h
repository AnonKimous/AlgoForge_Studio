#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtimesys/job_system.h directly. Use runtimesys/runtime_systems.h."
#endif

#include <cstddef>
#include <functional>
#include <string>

namespace runtimesys {

bool InitializeJobSystem(size_t worker_count = 7u);
void ShutdownJobSystem();
bool IsJobSystemInitialized();

enum class RuntimeJobPriority {
  High = 0,
  Normal = 1,
  Low = 2,
};

bool SubmitBlockingJob(
  RuntimeJobPriority priority,
  std::function<void(std::string* out_error_message)> body,
  std::string* out_error_message = nullptr);

}  // namespace runtimesys
