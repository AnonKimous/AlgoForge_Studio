#pragma once

#include "io_buffers.h"

#include <string>
#include <unordered_map>

namespace communication {

class SharedIoBus {
 public:
  void BindEndpoint(const std::string& endpoint_name, IoBufferEndpoint endpoint);
  bool WriteToEndpoint(const std::string& endpoint_name, const IoBufferPacket& packet);
  bool ReadFromEndpoint(const std::string& endpoint_name, IoBufferPacket* packet) const;

 private:
  std::unordered_map<std::string, IoBufferEndpoint> endpoints_;
};

class DedicatedIoLine {
 public:
  void BindEndpoint(IoBufferEndpoint endpoint);
  bool Write(const IoBufferPacket& packet);
  bool Read(IoBufferPacket* packet) const;

 private:
  IoBufferEndpoint endpoint_{};
};
}  // namespace communication
