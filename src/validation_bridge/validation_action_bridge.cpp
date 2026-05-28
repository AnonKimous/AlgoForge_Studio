#include "validation_action_bridge.h"

namespace {

PhysRunState ToPhysRunState(ValidationPhysRunState state) {
  switch (state) {
    case ValidationPhysRunState::Run: return PhysRunState::Run;
    case ValidationPhysRunState::Pause: return PhysRunState::Pause;
  }
  return PhysRunState::Pause;
}

}  // namespace

void ApplyValidationAction(ValidationAction action, VulkanRenderer& renderer, PhysModeController& phys_controller, Mesh& mesh) {
  switch (action.kind) {
    case ValidationActionKind::PhysStep: {
      const PhysRunState previous_run_state = renderer.phys_run_state();
      renderer.SetPhysRunState(PhysRunState::Pause);
      phys_controller.SetRunState(PhysRunState::Pause);
      const uint32_t step_count = action.step_count == 0 ? 1u : action.step_count;
      for (uint32_t i = 0; i < step_count; ++i) {
        phys_controller.StepOnce(mesh);
      }
      renderer.SetPhysRunState(previous_run_state);
      phys_controller.SetRunState(previous_run_state);
      break;
    }
    case ValidationActionKind::Reset:
      phys_controller.Reset(mesh);
      renderer.SetPhysRunState(PhysRunState::Pause);
      renderer.SetPhysGuideEnabled(true);
      phys_controller.SetRunState(PhysRunState::Pause);
      phys_controller.SetGuideEnabled(true);
      break;
    case ValidationActionKind::SetRunState:
      renderer.SetPhysRunState(ToPhysRunState(action.run_state));
      phys_controller.SetRunState(ToPhysRunState(action.run_state));
      break;
    case ValidationActionKind::SetGuideEnabled:
      renderer.SetPhysGuideEnabled(action.enabled);
      phys_controller.SetGuideEnabled(action.enabled);
      break;
  }
}

void ApplyValidationActions(const std::vector<ValidationAction>& actions, VulkanRenderer& renderer, PhysModeController& phys_controller, Mesh& mesh) {
  for (ValidationAction action : actions) {
    ApplyValidationAction(action, renderer, phys_controller, mesh);
  }
}
