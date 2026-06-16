#pragma once

#define RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD 1
#include "runtime_systems/memory_manager.h"
#undef RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD

#include <EASTL/allocator.h>

#include <cstddef>

namespace eastl {

class RuntimeSystemAllocator : public eastl::allocator {
 public:
  using size_type = size_t;

  explicit RuntimeSystemAllocator(const char* name = "runtimeSys") noexcept
    : eastl::allocator(name ? name : "runtimeSys") {}

  RuntimeSystemAllocator(const RuntimeSystemAllocator& other) noexcept = default;
  RuntimeSystemAllocator(const RuntimeSystemAllocator& other, const char* name) noexcept
    : eastl::allocator(other, name ? name : "runtimeSys") {}
  RuntimeSystemAllocator& operator=(const RuntimeSystemAllocator& other) noexcept = default;

  void* allocate(size_t n, int flags = 0) {
    return allocate(n, alignof(std::max_align_t), 0u, flags);
  }

  void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0) {
    (void)flags;
    return runtime_systems::MemoryManager::Instance().Allocate(n, alignment, offset);
  }

  void deallocate(void* p, size_t n) {
    (void)n;
    runtime_systems::MemoryManager::Instance().Deallocate(p);
  }
};

inline RuntimeSystemAllocator& DefaultAllocator() {
  static RuntimeSystemAllocator allocator{};
  return allocator;
}

inline RuntimeSystemAllocator* get_default_allocator(const RuntimeSystemAllocator*) {
  return &DefaultAllocator();
}

}  // namespace eastl
