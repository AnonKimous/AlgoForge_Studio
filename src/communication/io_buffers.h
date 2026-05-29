#pragma once

#include "../algorithm/algorithm_types.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

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
  uint32_t reflector_offset{};
  uint32_t reflector_length{};
};

using IoReflectorApplyCallback = std::function<void(
  const std::vector<std::byte>& data_bytes,
  void* receiver_context)>;

struct IoDataBufferReflector {
  std::string name;
  CreateDataReflectionInfo reflection_info{};
  IoReflectorApplyCallback apply_callback;
};

enum class IoDataBufferEntryKind {
  Data,
  Reflector,
};

struct IoDataBufferEntry {
  std::string name;
  IoDataBufferEntryKind kind{IoDataBufferEntryKind::Data};
  std::vector<std::byte> bytes;
  IoDataBufferReflector reflector{};
};

struct IoBufferPacket {
  IoProtocolDescriptor protocol{};
  std::vector<IoSignalBufferEntry> signal_buffer;
  std::vector<IoDataBufferEntry> data_buffer;
};

struct IoBufferEndpoint {
  IoProtocolDescriptor* protocol{};
  std::vector<IoSignalBufferEntry>* signal_buffer{};
  std::vector<IoDataBufferEntry>* data_buffer{};

  bool valid() const {
    return protocol != nullptr && signal_buffer != nullptr && data_buffer != nullptr;
  }
};

namespace data_protocol {
using ::IoBufferEndpoint;
using ::IoBufferPacket;
using ::IoDataBufferEntry;
using ::IoDataBufferEntryKind;
using ::IoDataBufferReflector;
using ::IoProtocolCompressionKind;
using ::IoProtocolDescriptor;
using ::IoReflectorApplyCallback;
using ::IoSignalBufferEntry;
}  // namespace data_protocol
