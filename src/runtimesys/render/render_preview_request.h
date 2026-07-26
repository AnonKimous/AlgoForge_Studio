#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace runtimesys {

struct RenderPreviewBuffer {
  std::string binding_name;
  uint32_t element_stride{0u};
  bool draw_indirect{false};
  std::vector<std::byte> bytes;
};

struct RenderPreviewCamera {
  std::array<float, 4> position{0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 4> target{0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 4> up{0.0f, 0.0f, 1.0f, 0.0f};
};

struct RenderPreviewRequest {
  const void* execution_key{nullptr};
  std::string stage_name;
  std::string vertex_shader_path;
  std::string fragment_shader_path;
  std::vector<RenderPreviewBuffer> storage_buffers;
  RenderPreviewCamera camera{};
  bool valid{false};

  void Clear() {
    execution_key = nullptr;
    stage_name.clear();
    vertex_shader_path.clear();
    fragment_shader_path.clear();
    storage_buffers.clear();
    camera = {};
    valid = false;
  }
};

}  // namespace runtimesys

