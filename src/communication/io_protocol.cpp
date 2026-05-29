#include "io_protocol.h"

#include <cstring>
#include <type_traits>

namespace {

struct _PackedPhysRuntimeControl {
  uint32_t run_state{};
  uint32_t guide_enabled{};
};

struct _PackedValidationAction {
  uint32_t kind{};
  uint32_t step_count{};
  uint32_t run_state{};
  uint32_t enabled{};
};

template <typename T>
void _AppendPod(std::vector<std::byte>* bytes, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!bytes) return;
  const size_t offset = bytes->size();
  bytes->resize(offset + sizeof(T));
  std::memcpy(bytes->data() + offset, &value, sizeof(T));
}

template <typename T>
bool _ReadPod(const std::vector<std::byte>& bytes, size_t* offset, T* value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!offset || !value) return false;
  if (*offset + sizeof(T) > bytes.size()) return false;
  std::memcpy(value, bytes.data() + *offset, sizeof(T));
  *offset += sizeof(T);
  return true;
}

}  // namespace

IoBufferPacket BuildPhysRuntimeControlIoPacket(PhysRunState run_state, bool guide_enabled) {
  IoBufferPacket packet{};
  packet.protocol.name = kPhysRuntimeControlIoProtocolName;
  packet.signal_buffer.push_back(IoSignalBufferEntry{"phys_runtime_control", 0u, 1u, 0u, 0u});

  IoDataBufferEntry entry{};
  entry.name = "phys_runtime_control_data";
  _AppendPod(&entry.bytes, _PackedPhysRuntimeControl{
    static_cast<uint32_t>(run_state == PhysRunState::Run ? 1u : 0u),
    guide_enabled ? 1u : 0u,
  });
  packet.data_buffer.push_back(std::move(entry));
  return packet;
}

bool DecodePhysRuntimeControlIoPacket(const IoBufferPacket& packet, PhysRunState* run_state, bool* guide_enabled) {
  if (!run_state || !guide_enabled) return false;
  if (packet.protocol.name != kPhysRuntimeControlIoProtocolName || packet.data_buffer.empty()) return false;

  size_t offset = 0;
  _PackedPhysRuntimeControl packed{};
  if (!_ReadPod(packet.data_buffer.front().bytes, &offset, &packed)) {
    return false;
  }

  *run_state = packed.run_state == 1u ? PhysRunState::Run : PhysRunState::Pause;
  *guide_enabled = packed.guide_enabled != 0u;
  return true;
}

IoBufferPacket BuildValidationActionsIoPacket(const std::vector<ValidationAction>& actions) {
  IoBufferPacket packet{};
  packet.protocol.name = kValidationActionsIoProtocolName;
  packet.signal_buffer.push_back(IoSignalBufferEntry{"validation_actions", 0u, 1u, 0u, 0u});

  IoDataBufferEntry entry{};
  entry.name = "validation_actions_data";
  _AppendPod(&entry.bytes, static_cast<uint32_t>(actions.size()));
  for (const ValidationAction& action : actions) {
    _AppendPod(&entry.bytes, _PackedValidationAction{
      static_cast<uint32_t>(action.kind),
      action.step_count,
      static_cast<uint32_t>(action.run_state),
      action.enabled ? 1u : 0u,
    });
  }
  packet.data_buffer.push_back(std::move(entry));
  return packet;
}

bool DecodeValidationActionsIoPacket(const IoBufferPacket& packet, std::vector<ValidationAction>* actions) {
  if (!actions) return false;
  if (packet.protocol.name != kValidationActionsIoProtocolName || packet.data_buffer.empty()) return false;

  size_t offset = 0;
  uint32_t action_count = 0;
  if (!_ReadPod(packet.data_buffer.front().bytes, &offset, &action_count)) {
    return false;
  }

  actions->clear();
  actions->reserve(action_count);
  for (uint32_t i = 0; i < action_count; ++i) {
    _PackedValidationAction packed{};
    if (!_ReadPod(packet.data_buffer.front().bytes, &offset, &packed)) {
      return false;
    }

    ValidationAction action{};
    action.kind = static_cast<ValidationActionKind>(packed.kind);
    action.step_count = packed.step_count;
    action.run_state = static_cast<ValidationPhysRunState>(packed.run_state);
    action.enabled = packed.enabled != 0u;
    actions->push_back(action);
  }

  return true;
}
