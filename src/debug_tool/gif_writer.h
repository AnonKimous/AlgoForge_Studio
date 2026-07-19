#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace debug_tool {

class GifWriter {
 public:
  void Open(const std::filesystem::path& path, uint32_t width, uint32_t height, uint16_t delay_centiseconds) {
    if (width == 0u || height == 0u || width > 65535u || height > 65535u) {
      throw std::runtime_error("GIF dimensions must be between 1 and 65535 pixels.");
    }
    if (stream_.is_open()) {
      throw std::runtime_error("GIF writer is already open.");
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      throw std::runtime_error("Failed to create GIF output directory: " + path.parent_path().string());
    }
    stream_.open(path, std::ios::binary | std::ios::trunc);
    if (!stream_) {
      throw std::runtime_error("Failed to open GIF output: " + path.string());
    }
    path_ = path;
    width_ = width;
    height_ = height;
    delay_centiseconds_ = delay_centiseconds;
    frame_count_ = 0u;

    stream_.write("GIF89a", 6);
    WriteU16(static_cast<uint16_t>(width_));
    WriteU16(static_cast<uint16_t>(height_));
    stream_.put(static_cast<char>(0xF7));
    stream_.put(static_cast<char>(0x00));
    stream_.put(static_cast<char>(0x00));
    for (uint32_t index = 0u; index < 256u; ++index) {
      const uint8_t red = static_cast<uint8_t>(((index >> 5u) & 0x07u) * 255u / 7u);
      const uint8_t green = static_cast<uint8_t>(((index >> 2u) & 0x07u) * 255u / 7u);
      const uint8_t blue = static_cast<uint8_t>((index & 0x03u) * 255u / 3u);
      stream_.put(static_cast<char>(red));
      stream_.put(static_cast<char>(green));
      stream_.put(static_cast<char>(blue));
    }
  }

  void WriteRgbaFrame(const std::vector<std::byte>& rgba) {
    WriteRgbaFrame(rgba, delay_centiseconds_);
  }

  void WriteRgbaFrame(const std::vector<std::byte>& rgba, uint16_t delay_centiseconds) {
    if (!stream_.is_open()) {
      throw std::runtime_error("GIF writer is not open.");
    }
    const size_t expected_size = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u;
    if (rgba.size() != expected_size) {
      throw std::runtime_error("GIF frame size does not match the logical screen.");
    }

    stream_.put(static_cast<char>(0x21));
    stream_.put(static_cast<char>(0xF9));
    stream_.put(static_cast<char>(0x04));
    stream_.put(static_cast<char>(0x00));
    WriteU16(delay_centiseconds);
    stream_.put(static_cast<char>(0x00));
    stream_.put(static_cast<char>(0x00));
    stream_.put(static_cast<char>(0x2C));
    WriteU16(0u);
    WriteU16(0u);
    WriteU16(static_cast<uint16_t>(width_));
    WriteU16(static_cast<uint16_t>(height_));
    stream_.put(static_cast<char>(0x00));
    stream_.put(static_cast<char>(0x08));

    std::vector<uint8_t> indices(static_cast<size_t>(width_) * static_cast<size_t>(height_));
    for (size_t pixel = 0u; pixel < indices.size(); ++pixel) {
      const size_t offset = pixel * 4u;
      const uint8_t red = std::to_integer<uint8_t>(rgba[offset + 0u]);
      const uint8_t green = std::to_integer<uint8_t>(rgba[offset + 1u]);
      const uint8_t blue = std::to_integer<uint8_t>(rgba[offset + 2u]);
      indices[pixel] = static_cast<uint8_t>((red & 0xE0u) | ((green & 0xE0u) >> 3u) | (blue >> 6u));
    }
    const std::vector<uint8_t> compressed = Compress(indices);
    size_t offset = 0u;
    while (offset < compressed.size()) {
      const size_t block_size = std::min<size_t>(255u, compressed.size() - offset);
      stream_.put(static_cast<char>(block_size));
      stream_.write(reinterpret_cast<const char*>(compressed.data() + offset), static_cast<std::streamsize>(block_size));
      offset += block_size;
    }
    stream_.put(static_cast<char>(0x00));
    ++frame_count_;
  }

  void Close() {
    if (!stream_.is_open()) {
      throw std::runtime_error("GIF writer is not open.");
    }
    stream_.put(static_cast<char>(0x3B));
    stream_.close();
    if (!stream_) {
      throw std::runtime_error("Failed while closing GIF output: " + path_.string());
    }
  }

  uint32_t frame_count() const {
    return frame_count_;
  }

 private:
  void WriteU16(uint16_t value) {
    stream_.put(static_cast<char>(value & 0xFFu));
    stream_.put(static_cast<char>((value >> 8u) & 0xFFu));
  }

  static std::vector<uint8_t> Compress(const std::vector<uint8_t>& indices) {
    constexpr uint16_t clear_code = 256u;
    constexpr uint16_t end_code = 257u;
    std::unordered_map<uint32_t, uint16_t> dictionary;
    dictionary.reserve(4096u);
    std::vector<uint8_t> output;
    output.reserve(indices.size() / 2u + 32u);
    uint32_t bit_buffer = 0u;
    uint32_t bit_count = 0u;
    const auto write_code = [&](uint16_t code, uint32_t code_size) {
      bit_buffer |= static_cast<uint32_t>(code) << bit_count;
      bit_count += code_size;
      while (bit_count >= 8u) {
        output.push_back(static_cast<uint8_t>(bit_buffer & 0xFFu));
        bit_buffer >>= 8u;
        bit_count -= 8u;
      }
    };
    const auto reset = [&]() {
      dictionary.clear();
      return std::pair<uint32_t, uint16_t>{9u, 258u};
    };

    uint32_t code_size = 9u;
    uint16_t next_code = 258u;
    write_code(clear_code, code_size);
    uint16_t prefix = indices.front();
    for (size_t index = 1u; index < indices.size(); ++index) {
      const uint8_t symbol = indices[index];
      const uint32_t key = (static_cast<uint32_t>(prefix) << 8u) | symbol;
      const auto found = dictionary.find(key);
      if (found != dictionary.end()) {
        prefix = found->second;
        continue;
      }
      write_code(prefix, code_size);
      if (next_code < 510u) {
        dictionary.emplace(key, next_code++);
      } else {
        write_code(clear_code, code_size);
        const auto reset_state = reset();
        code_size = reset_state.first;
        next_code = reset_state.second;
      }
      prefix = symbol;
    }
    write_code(prefix, code_size);
    write_code(end_code, code_size);
    if (bit_count != 0u) {
      output.push_back(static_cast<uint8_t>(bit_buffer & 0xFFu));
    }
    return output;
  }

  std::ofstream stream_{};
  std::filesystem::path path_{};
  uint32_t width_{0u};
  uint32_t height_{0u};
  uint16_t delay_centiseconds_{0u};
  uint32_t frame_count_{0u};
};

}  // namespace debug_tool
