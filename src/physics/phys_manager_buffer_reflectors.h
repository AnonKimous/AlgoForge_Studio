#pragma once

#include "phys_manager.h"

namespace core_services {

struct PhysManagerBufferDecodeContext {
  std::vector<VelocityGuidance>* guidances{};
  std::vector<VelocityGuideVelocity>* guide_velocities{};
  std::vector<VelocityGuideForce>* guide_forces{};
};

IoDataBufferEntry CreateVelocityGuidanceReflectorBufferEntry();
IoDataBufferEntry CreateGuideVelocityReflectorBufferEntry();
IoDataBufferEntry CreateGuideForceReflectorBufferEntry();

IoDataBufferEntry CreateVelocityGuidanceDataBufferEntry(const std::vector<VelocityGuidance>& guidances);
IoDataBufferEntry CreateGuideVelocityDataBufferEntry(const std::vector<VelocityGuideVelocity>& guide_velocities);
IoDataBufferEntry CreateGuideForceDataBufferEntry(const std::vector<VelocityGuideForce>& guide_forces);

}  // namespace core_services
