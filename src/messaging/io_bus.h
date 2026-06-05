#pragma once

#if !defined(MESSAGING_LAYER_INTERNAL_BUILD) && !defined(MESSAGING_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include messaging/io_bus.h directly. Use messaging/messaging.h."
#endif

#include "io_buffers.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace messaging {

class SharedIoBus {
 public:
  IoBufferEndpoint AllocateBuffer(const std::string& module_name, uint32_t buffer_id = 0, bool lock_required = false);
  std::array<IoBufferEndpoint, 2> AllocateFastChannel(const std::string& channel_name, bool lock_required = true);
  void Clear();

  bool PublishToBuffer(const std::string& module_name, uint32_t buffer_id, const IoBufferPacket& packet);
  bool PublishToFastChannel(const std::string& channel_name, const IoBufferPacket& packet);
  bool ReadFromBuffer(const std::string& module_name, uint32_t buffer_id, IoBufferPacket* packet) const;
  bool ReadFromFastChannel(const std::string& channel_name, IoBufferPacket* packet) const;

 private:
  struct BufferStorage {
    IoBufferPacket packet{};
    bool lock_required{false};
  };

  using BufferMap = std::unordered_map<uint32_t, std::unique_ptr<BufferStorage>>;

  std::unordered_map<std::string, BufferMap> module_buffers_;

  BufferStorage* EnsureBufferStorage(const std::string& module_name, uint32_t buffer_id, bool lock_required);
  BufferStorage* FindBufferStorage(const std::string& module_name, uint32_t buffer_id);
  const BufferStorage* FindBufferStorage(const std::string& module_name, uint32_t buffer_id) const;
  BufferStorage* FindFastChannelStorage(const std::string& channel_name);
  const BufferStorage* FindFastChannelStorage(const std::string& channel_name) const;
  static IoBufferEndpoint MakeEndpoint(BufferStorage* storage);
  static IoBufferEndpoint MakeEndpoint(const BufferStorage* storage);
  static void CopyPacket(IoBufferPacket* destination, const IoBufferPacket& source);
};

}  // namespace messaging

using messaging::SharedIoBus;
