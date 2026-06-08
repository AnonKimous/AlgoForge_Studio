#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace runtime_systems {

struct RenderPreviewBuffer {
  std::string binding_name;
  uint32_t element_stride{0u};
  std::vector<std::byte> bytes;
};

struct RenderPreviewRequest {
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::vector<RenderPreviewBuffer> storage_buffers;
  uint32_t instance_count{0u};
  bool valid{false};

  void Clear() {
    stage_name.clear();
    vertex_shader_path.clear();
    fragment_shader_path.clear();
    storage_buffers.clear();
    instance_count = 0u;
    valid = false;
  }
};

}  // namespace runtime_systems

