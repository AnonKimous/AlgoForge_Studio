#include "agents.h"

#include "codec/codec_manager.h"

namespace agents {

void AgentAlgorithmRuntime::Init(
  const PhysSolverConfig& config,
  const VulkanComputeContextView& compute_context,
  const AlgorithmComplianceDescriptor& compliance_descriptor) {
  pool_.Init(config, compute_context, compliance_descriptor);
}

bool AgentAlgorithmRuntime::Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const {
  return pool_.Run(request, result);
}

void AgentAlgorithmRuntime::SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package) {
  pool_.SetInterventionPackage(std::move(package));
}

void AgentAlgorithmRuntime::ApplyInterventionRequest(const InteractionInterventionRequest& request) {
  intervention_request_ = request;
  algorithm_to_agent_signal_.intervention_applied = false;
  algorithm_to_agent_signal_.intervention_needed = request.enabled;

  CodecManager codec{};
  IoBufferPacket packet = codec.BuildAlgorithmInterventionPacket(request);
  InteractionInterventionRequest decoded{};
  if (codec.DecodeAlgorithmInterventionPacket(packet, &decoded)) {
    decoded.enabled = request.enabled;
    intervention_request_ = decoded;
    algorithm_to_agent_signal_.intervention_applied = request.enabled;
  }
}

bool WindowAgent::Init(const char* title, int width, int height) {
  window_ = std::make_unique<SdlWindow>(title, width, height);
  imgui_runtime_ = std::make_unique<runtime_systems::ImGuiVulkanRuntime>();
  if (!imgui_runtime_->Init(window_->native_handle().window, title ? title : "Entity Debug UI")) {
    imgui_runtime_.reset();
    window_.reset();
    return false;
  }
  return true;
}

bool WindowAgent::Tick() {
  if (!window_ || !window_->ProcessEvents()) {
    return false;
  }
  return !imgui_runtime_ || imgui_runtime_->Tick(window_->native_handle().window);
}

void WindowAgent::Destroy() {
  if (imgui_runtime_) {
    imgui_runtime_->Destroy();
    imgui_runtime_.reset();
  }
  window_.reset();
}

WindowHandle WindowAgent::native_handle() const {
  return window_ ? window_->native_handle() : WindowHandle{};
}

int WindowAgent::width() const {
  return window_ ? window_->width() : 1;
}

int WindowAgent::height() const {
  return window_ ? window_->height() : 1;
}

const InputState& WindowAgent::input() const {
  static const InputState kEmptyInput{};
  return window_ ? window_->input() : kEmptyInput;
}

Vec2 WindowAgent::MousePosition() const {
  return window_ ? window_->MousePosition() : Vec2{};
}

bool RenderAgent::Init(const WindowHandle& window_handle) {
  (void)window_handle;
  return true;
}

InteractionUiAction RenderAgent::Tick(const Mesh& mesh, const InteractionUiState& ui_state) {
  (void)mesh;
  InteractionUiAction action{};
  action.mode = mode_;
  action.phys_run_state = phys_run_state_;
  action.agent_to_algorithm_signal = ui_state.agent_to_algorithm_signal;
  action.intervention_request = ui_state.intervention_request;
  return action;
}

void RenderAgent::Destroy() {
  mode_ = InteractionMode::Edit;
  phys_run_state_ = PhysRunState::Pause;
}

InteractionMode RenderAgent::mode() const {
  return mode_;
}

PhysRunState RenderAgent::phys_run_state() const {
  return phys_run_state_;
}

void RenderAgent::SetPhysRunState(PhysRunState state) {
  phys_run_state_ = state;
}

}  // namespace agents
