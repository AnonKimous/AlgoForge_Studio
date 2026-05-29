#include "io_bus.h"

namespace communication {

void SharedIoBus::BindEndpoint(const std::string& endpoint_name, IoBufferEndpoint endpoint) {
  endpoints_[endpoint_name] = endpoint;
}

bool SharedIoBus::WriteToEndpoint(const std::string& endpoint_name, const IoBufferPacket& packet) {
  auto it = endpoints_.find(endpoint_name);
  if (it == endpoints_.end() || !it->second.valid()) {
    return false;
  }

  *it->second.protocol = packet.protocol;
  *it->second.signal_buffer = packet.signal_buffer;
  *it->second.data_buffer = packet.data_buffer;
  return true;
}

bool SharedIoBus::ReadFromEndpoint(const std::string& endpoint_name, IoBufferPacket* packet) const {
  if (!packet) {
    return false;
  }

  auto it = endpoints_.find(endpoint_name);
  if (it == endpoints_.end() || !it->second.valid()) {
    return false;
  }

  packet->protocol = *it->second.protocol;
  packet->signal_buffer = *it->second.signal_buffer;
  packet->data_buffer = *it->second.data_buffer;
  return true;
}

void DedicatedIoLine::BindEndpoint(IoBufferEndpoint endpoint) {
  endpoint_ = endpoint;
}

bool DedicatedIoLine::Write(const IoBufferPacket& packet) {
  if (!endpoint_.valid()) {
    return false;
  }

  *endpoint_.protocol = packet.protocol;
  *endpoint_.signal_buffer = packet.signal_buffer;
  *endpoint_.data_buffer = packet.data_buffer;
  return true;
}

bool DedicatedIoLine::Read(IoBufferPacket* packet) const {
  if (!packet || !endpoint_.valid()) {
    return false;
  }

  packet->protocol = *endpoint_.protocol;
  packet->signal_buffer = *endpoint_.signal_buffer;
  packet->data_buffer = *endpoint_.data_buffer;
  return true;
}

}  // namespace communication
