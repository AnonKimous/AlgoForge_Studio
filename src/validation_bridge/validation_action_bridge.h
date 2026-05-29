#pragma once

#include "../interaction/phys_mode_controller.h"
#include "../mesh.h"
#include "../render/renderer.h"
#include "../validation_layer/validation_actions.h"

#include <vector>

namespace interaction_analysis {

void ApplyValidationAction(ValidationAction action, VulkanRenderer& renderer, PhysModeController& phys_controller, Mesh& mesh);
void ApplyValidationActions(const std::vector<ValidationAction>& actions, VulkanRenderer& renderer, PhysModeController& phys_controller, Mesh& mesh);

}  // namespace interaction_analysis
