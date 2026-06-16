#include "memory_manager.h"

#include <cassert>
#include <new>

#include <mimalloc.h>

namespace runtime_systems {

namespace {

[[noreturn]] void _AbortMemoryAllocation(const char* message) {
  assert(false && message);
  throw std::bad_alloc{};
}

}  // namespace

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

void* MemoryManager::Allocate(size_t bytes, size_t alignment, size_t offset) {
  const size_t requested_bytes = bytes == 0u ? 1u : bytes;
  const size_t requested_alignment = alignment == 0u ? alignof(std::max_align_t) : alignment;

  if (offset != 0u) {
    _AbortMemoryAllocation("MemoryManager offset-based allocation is not supported by the mimalloc backend.");
  }

  void* allocated = mi_malloc_aligned(requested_bytes, requested_alignment);
  if (!allocated) {
    _AbortMemoryAllocation("MemoryManager allocation failed.");
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_allocation_sizes_[allocated] = requested_bytes;
    ++statistics_.active_allocations;
    statistics_.active_bytes += requested_bytes;
    ++statistics_.total_allocations;
    statistics_.total_bytes += requested_bytes;
  }

  return allocated;
}

void MemoryManager::Deallocate(void* p) {
  if (!p) {
    return;
  }

  size_t tracked_bytes = 0u;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = active_allocation_sizes_.find(p);
    if (found != active_allocation_sizes_.end()) {
      tracked_bytes = found->second;
      active_allocation_sizes_.erase(found);
    } else {
      _AbortMemoryAllocation("Attempted to deallocate memory that was not tracked by MemoryManager.");
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

  mi_free(p);
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
  return Allocate(bytes, alignment);
}

void MemoryManager::do_deallocate(void* p, size_t bytes, size_t alignment) {
  (void)bytes;
  (void)alignment;
  Deallocate(p);
}

bool MemoryManager::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
  return this == &other;
}

}  // namespace runtime_systems
