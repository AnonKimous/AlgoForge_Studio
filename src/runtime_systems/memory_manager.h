#pragma once

#include <cstddef>
#include <memory_resource>
#include <mutex>
#include <unordered_map>

namespace runtime_systems {

struct MemoryStatistics {
  size_t active_allocations{0u};
  size_t active_bytes{0u};
  size_t total_allocations{0u};
  size_t total_bytes{0u};
};

class MemoryManager final : public std::pmr::memory_resource {
 public:
  static MemoryManager& Instance();

  MemoryStatistics statistics() const;
  void ResetStatistics();

 private:
  MemoryManager();
  ~MemoryManager() override;

  void* do_allocate(size_t bytes, size_t alignment) override;
  void do_deallocate(void* p, size_t bytes, size_t alignment) override;
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

  mutable std::mutex mutex_{};
  std::unordered_map<void*, size_t> active_allocation_sizes_{};
  MemoryStatistics statistics_{};
  std::pmr::memory_resource* previous_default_resource_{nullptr};
};

}  // namespace runtime_systems
