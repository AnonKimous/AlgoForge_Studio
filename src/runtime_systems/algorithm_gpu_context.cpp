#include "algorithm_gpu_context.h"

namespace runtime_systems {

AlgorithmGpuContextRegistry& AlgorithmGpuContextRegistry::Instance() {
  static AlgorithmGpuContextRegistry instance{};
  return instance;
}

void AlgorithmGpuContextRegistry::Set(AlgorithmGpuExecutionContext context) {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = context;
}

AlgorithmGpuExecutionContext AlgorithmGpuContextRegistry::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_;
}

bool AlgorithmGpuContextRegistry::HasContext() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_.valid();
}

void AlgorithmGpuContextRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = {};
}

}  // namespace runtime_systems

