#include "runtime_systems/runtime_gpu_context.h"

namespace runtime_systems {

RuntimeGpuContextRegistry& RuntimeGpuContextRegistry::Instance() {
  static RuntimeGpuContextRegistry instance{};
  return instance;
}

void RuntimeGpuContextRegistry::Set(RuntimeGpuExecutionContext context) {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = context;
}

RuntimeGpuExecutionContext RuntimeGpuContextRegistry::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_;
}

bool RuntimeGpuContextRegistry::HasContext() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_.valid();
}

void RuntimeGpuContextRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = {};
}

}  // namespace runtime_systems
