#include "runtimesys/runtime_vk_context.h"

#include <utility>

namespace runtimesys {

RuntimeVkContextRegistry& RuntimeVkContextRegistry::Instance() {
  static RuntimeVkContextRegistry instance{};
  return instance;
}

void RuntimeVkContextRegistry::Set(RuntimeVkExecutionContext context) {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = context;
  algorithm_queues_.clear();
  algorithm_queue_first_index_ = 1u;
  queue_assignments_.clear();
  execution_progress_.clear();
  result_images_.clear();
  next_algorithm_queue_index_ = 0u;
}

void RuntimeVkContextRegistry::SetAlgorithmQueues(
  std::vector<VkQueue> queues,
  uint32_t first_queue_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  algorithm_queues_ = std::move(queues);
  algorithm_queue_first_index_ = first_queue_index;
  queue_assignments_.clear();
  execution_progress_.clear();
  result_images_.clear();
  next_algorithm_queue_index_ = 0u;
}

void RuntimeVkContextRegistry::PublishExecutionProgress(const void* execution_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++execution_progress_[execution_key];
}

uint64_t RuntimeVkContextRegistry::SnapshotExecutionProgress(const void* execution_key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = execution_progress_.find(execution_key);
  return found == execution_progress_.end() ? 0u : found->second;
}

RuntimeVkExecutionContext RuntimeVkContextRegistry::Snapshot(const void* execution_key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  RuntimeVkExecutionContext result = context_;
  if (execution_key == nullptr) {
    return result;
  }
  if (algorithm_queues_.empty()) {
    return {};
  }
  auto assignment = queue_assignments_.find(execution_key);
  if (assignment == queue_assignments_.end()) {
    const uint32_t queue_index = next_algorithm_queue_index_++ %
      static_cast<uint32_t>(algorithm_queues_.size());
    assignment = queue_assignments_.emplace(execution_key, queue_index).first;
  }
  result.queue = algorithm_queues_[assignment->second];
  result.queue_index = algorithm_queue_first_index_ + assignment->second;
  return result;
}

void RuntimeVkContextRegistry::PublishResultImage(
  const void* execution_key,
  RuntimeVkResultImage image) {
  std::lock_guard<std::mutex> lock(mutex_);
  result_images_[execution_key] = image;
}

RuntimeVkResultImage RuntimeVkContextRegistry::SnapshotResultImage(const void* execution_key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = result_images_.find(execution_key);
  return found == result_images_.end() ? RuntimeVkResultImage{} : found->second;
}

void RuntimeVkContextRegistry::ClearTransientState() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_assignments_.clear();
  execution_progress_.clear();
  result_images_.clear();
  next_algorithm_queue_index_ = 0u;
}

bool RuntimeVkContextRegistry::HasContext() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return context_.valid();
}

void RuntimeVkContextRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  context_ = {};
  algorithm_queues_.clear();
  algorithm_queue_first_index_ = 1u;
  queue_assignments_.clear();
  execution_progress_.clear();
  result_images_.clear();
  next_algorithm_queue_index_ = 0u;
}

}  // namespace runtimesys
