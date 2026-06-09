#include "memory_manager.h"

namespace runtime_systems {

MemoryManager& MemoryManager::Instance() {
  // Keep the manager alive for the whole process so tracked pmr containers
  // never outlive their backing resource during static teardown.
  static MemoryManager* instance = new MemoryManager();
  return *instance;
}

MemoryManager::MemoryManager()
  : previous_default_resource_(std::pmr::get_default_resource()) {
  std::pmr::set_default_resource(this);
}

MemoryManager::~MemoryManager() {
  std::pmr::set_default_resource(previous_default_resource_);
}

MemoryStatistics MemoryManager::statistics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return statistics_;
}

void MemoryManager::ResetStatistics() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_allocation_sizes_.clear();
  statistics_ = {};
}

void* MemoryManager::do_allocate(size_t bytes, size_t alignment) {
  void* allocated = std::pmr::new_delete_resource()->allocate(bytes, alignment);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_allocation_sizes_[allocated] = bytes;
    ++statistics_.active_allocations;
    statistics_.active_bytes += bytes;
    ++statistics_.total_allocations;
    statistics_.total_bytes += bytes;
  }

  return allocated;
}

void MemoryManager::do_deallocate(void* p, size_t bytes, size_t alignment) {
  size_t tracked_bytes = bytes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = active_allocation_sizes_.find(p);
    if (found != active_allocation_sizes_.end()) {
      tracked_bytes = found->second;
      active_allocation_sizes_.erase(found);
    }
    if (statistics_.active_allocations > 0u) {
      --statistics_.active_allocations;
    }
    if (statistics_.active_bytes >= tracked_bytes) {
      statistics_.active_bytes -= tracked_bytes;
    } else {
      statistics_.active_bytes = 0u;
    }
  }

  std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
}

bool MemoryManager::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
  return this == &other;
}

}  // namespace runtime_systems
