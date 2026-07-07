#include "runtime_systems/runtime_vk_context.h"

namespace runtime_systems {

RuntimeVkContextRegistry& RuntimeVkContextRegistry::Instance() {
  static RuntimeVkContextRegistry instance{};
  return instance;
}

void RuntimeVkContextRegistry::Set(RuntimeVkExecutionContext context) {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = context;
}

RuntimeVkExecutionContext RuntimeVkContextRegistry::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_;
}

bool RuntimeVkContextRegistry::HasContext() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_.valid();
}

void RuntimeVkContextRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = {};
}

}  // namespace runtime_systems
