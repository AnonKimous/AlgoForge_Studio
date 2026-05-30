#include "io_bus.h"

#include <cstring>

#include <utility>

namespace messaging {

namespace {

void CopyPacketImpl(IoBufferPacket* destination, const IoBufferPacket& source) {
  if (!destination) return;
  destination->protocol = source.protocol;
  destination->signal_buffer = source.signal_buffer;
  destination->data_buffer = source.data_buffer;
}

}  // namespace

IoBufferEndpoint SharedIoBus::MakeEndpoint(BufferStorage* storage) {
  if (!storage) return IoBufferEndpoint{};
  return IoBufferEndpoint{&storage->packet.protocol, &storage->packet.signal_buffer, &storage->packet.data_buffer};
}

IoBufferEndpoint SharedIoBus::MakeEndpoint(const BufferStorage* storage) {
  if (!storage) return IoBufferEndpoint{};
  return IoBufferEndpoint{
    const_cast<IoProtocolDescriptor*>(&storage->packet.protocol),
    const_cast<std::vector<IoSignalBufferEntry>*>(&storage->packet.signal_buffer),
    const_cast<std::vector<IoDataBufferEntry>*>(&storage->packet.data_buffer)};
}

SharedIoBus::BufferStorage* SharedIoBus::EnsureBufferStorage(const std::string& module_name, uint32_t buffer_id, bool lock_required) {
  auto& buffers = module_buffers_[module_name];
  auto it = buffers.find(buffer_id);
  if (it != buffers.end()) {
    if (lock_required) {
      it->second->lock_required = true;
    }
    return it->second.get();
  }

  auto storage = std::make_unique<BufferStorage>();
  storage->lock_required = lock_required;
  storage->packet.signal_buffer.reserve(2);
  storage->packet.data_buffer.reserve(2);
  BufferStorage* raw = storage.get();
  buffers.emplace(buffer_id, std::move(storage));
  return raw;
}

SharedIoBus::BufferStorage* SharedIoBus::FindBufferStorage(const std::string& module_name, uint32_t buffer_id) {
  auto module_it = module_buffers_.find(module_name);
  if (module_it == module_buffers_.end()) {
    return nullptr;
  }
  auto buffer_it = module_it->second.find(buffer_id);
  if (buffer_it == module_it->second.end()) {
    return nullptr;
  }
  return buffer_it->second.get();
}

const SharedIoBus::BufferStorage* SharedIoBus::FindBufferStorage(const std::string& module_name, uint32_t buffer_id) const {
  auto module_it = module_buffers_.find(module_name);
  if (module_it == module_buffers_.end()) {
    return nullptr;
  }
  auto buffer_it = module_it->second.find(buffer_id);
  if (buffer_it == module_it->second.end()) {
    return nullptr;
  }
  return buffer_it->second.get();
}

SharedIoBus::BufferStorage* SharedIoBus::FindFastChannelStorage(const std::string& channel_name) {
  return FindBufferStorage(channel_name, 0u);
}

const SharedIoBus::BufferStorage* SharedIoBus::FindFastChannelStorage(const std::string& channel_name) const {
  return FindBufferStorage(channel_name, 0u);
}

IoBufferEndpoint SharedIoBus::AllocateBuffer(const std::string& module_name, uint32_t buffer_id, bool lock_required) {
  return MakeEndpoint(EnsureBufferStorage(module_name, buffer_id, lock_required));
}

std::array<IoBufferEndpoint, 2> SharedIoBus::AllocateFastChannel(const std::string& channel_name, bool lock_required) {
  BufferStorage* storage = EnsureBufferStorage(channel_name, 0u, lock_required);
  return {MakeEndpoint(storage), MakeEndpoint(storage)};
}

void SharedIoBus::Clear() {
  module_buffers_.clear();
}

void SharedIoBus::CopyPacket(IoBufferPacket* destination, const IoBufferPacket& source) {
  CopyPacketImpl(destination, source);
}

bool SharedIoBus::PublishToBuffer(const std::string& module_name, uint32_t buffer_id, const IoBufferPacket& packet) {
  BufferStorage* storage = FindBufferStorage(module_name, buffer_id);
  if (!storage) {
    return false;
  }
  CopyPacket(&storage->packet, packet);
  return true;
}

bool SharedIoBus::PublishToFastChannel(const std::string& channel_name, const IoBufferPacket& packet) {
  BufferStorage* storage = FindFastChannelStorage(channel_name);
  if (!storage) {
    return false;
  }
  CopyPacket(&storage->packet, packet);
  return true;
}

bool SharedIoBus::ReadFromBuffer(const std::string& module_name, uint32_t buffer_id, IoBufferPacket* packet) const {
  if (!packet) {
    return false;
  }
  const BufferStorage* storage = FindBufferStorage(module_name, buffer_id);
  if (!storage) {
    return false;
  }
  CopyPacket(packet, storage->packet);
  return true;
}

bool SharedIoBus::ReadFromFastChannel(const std::string& channel_name, IoBufferPacket* packet) const {
  if (!packet) {
    return false;
  }
  const BufferStorage* storage = FindFastChannelStorage(channel_name);
  if (!storage) {
    return false;
  }
  CopyPacket(packet, storage->packet);
  return true;
}

}  // namespace messaging
