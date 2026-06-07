#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace common_data {

enum class IoProtocolCompressionKind {
  None,
};

struct IoProtocolDescriptor {
  std::string name;
  uint32_t version{1u};
  IoProtocolCompressionKind compression{IoProtocolCompressionKind::None};
};

struct IoSignalBufferEntry {
  std::string name;
  uint32_t data_offset{};
  uint32_t data_length{};
  std::string source_module_name;
  uint32_t source_buffer_id{};
  std::string target_module_name;
  uint32_t target_buffer_id{};
  bool lock_required{false};
};

struct IoDataBufferEntry {
  std::string name;
  std::vector<std::byte> bytes;
  uint32_t buffer_id{};
  uint32_t source_buffer_id{};
};

struct IoBufferPacket {
  IoProtocolDescriptor protocol{};
  std::vector<IoSignalBufferEntry> signal_buffer;
  std::vector<IoDataBufferEntry> data_buffer;
};

}  // namespace common_data

using common_data::IoBufferPacket;
using common_data::IoDataBufferEntry;
using common_data::IoProtocolCompressionKind;
using common_data::IoProtocolDescriptor;
using common_data::IoSignalBufferEntry;
