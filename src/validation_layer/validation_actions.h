#pragma once

#include "validation_snapshot.h"

#include <cstdint>

enum class ValidationActionKind {
  PhysStep,
  Reset,
  SetRunState,
  SetGuideEnabled,
};

struct ValidationAction {
  ValidationActionKind kind{ValidationActionKind::PhysStep};
  uint32_t step_count{1};
  ValidationPhysRunState run_state{ValidationPhysRunState::Pause};
  bool enabled{true};
};
