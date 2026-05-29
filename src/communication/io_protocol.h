#pragma once

#include "io_buffers.h"

#include "../physics/physics_types.h"
#include "../validation_layer/validation_actions.h"

#include <vector>

inline constexpr const char* kGuideUiPhysIoProtocolName = "guide_ui_phys_v1";
inline constexpr const char* kPhysRuntimeControlIoProtocolName = "phys_runtime_control_v1";
inline constexpr const char* kValidationActionsIoProtocolName = "validation_actions_v1";

IoBufferPacket BuildPhysRuntimeControlIoPacket(PhysRunState run_state, bool guide_enabled);
bool DecodePhysRuntimeControlIoPacket(const IoBufferPacket& packet, PhysRunState* run_state, bool* guide_enabled);

IoBufferPacket BuildValidationActionsIoPacket(const std::vector<ValidationAction>& actions);
bool DecodeValidationActionsIoPacket(const IoBufferPacket& packet, std::vector<ValidationAction>* actions);

namespace data_protocol {
using ::BuildPhysRuntimeControlIoPacket;
using ::BuildValidationActionsIoPacket;
using ::DecodePhysRuntimeControlIoPacket;
using ::DecodeValidationActionsIoPacket;
using ::kGuideUiPhysIoProtocolName;
using ::kPhysRuntimeControlIoProtocolName;
using ::kValidationActionsIoProtocolName;
}  // namespace data_protocol
