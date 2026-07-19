#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace runtimesys {

struct DebugToolRecordedFrame {
  uint32_t width{0u};
  uint32_t height{0u};
  uint64_t tick_sequence{0u};
  std::vector<std::byte> rgba{};
};

}  // namespace runtimesys
