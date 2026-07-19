#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace runtimesys {

struct RenderPreviewBuffer {
  std::string binding_name;
  uint32_t element_stride{0u};
  bool draw_indirect{false};
  std::vector<std::byte> bytes;
};

struct RenderPreviewRequest {
  const void* execution_key{nullptr};
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::vector<RenderPreviewBuffer> storage_buffers;
  bool valid{false};

  void Clear() {
    execution_key = nullptr;
    stage_name.clear();
    vertex_shader_path.clear();
    fragment_shader_path.clear();
    storage_buffers.clear();
    valid = false;
  }
};

}  // namespace runtimesys

