#include "module_agents.h"

#include "../communication/io_protocol.h"
#include "../algorithm/triangle_orientation_cpu_algorithm.h"

namespace agents {

bool WindowAgent::Init(const char* title, int width, int height) {
  window_ = std::make_unique<SdlWindow>(title, width, height);
  return true;
}

bool WindowAgent::Tick() {
  return window_ && window_->ProcessEvents();
}

void WindowAgent::Destroy() {
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

void SceneViewAgent::Init(const Mesh& mesh) {
  camera_.FitToMesh(mesh);
  viewport_ = ViewportTransform{};
}

SceneViewFrameState SceneViewAgent::Tick(const SceneViewBounds& scene_bounds, const WindowAgent& window_agent) {
  SceneViewFrameState frame{};
  frame.mouse_pixel = window_agent.MousePosition();
  if (scene_bounds.valid) {
    frame.mouse_pixel.x -= scene_bounds.x;
    frame.mouse_pixel.y -= scene_bounds.y;
    viewport_.SetSize(static_cast<int>(scene_bounds.width), static_cast<int>(scene_bounds.height));
    camera_.SetViewportSize(static_cast<int>(scene_bounds.width), static_cast<int>(scene_bounds.height));
  } else {
    viewport_.SetSize(window_agent.width(), window_agent.height());
  }
  return frame;
}

void SceneViewAgent::Destroy() {
  camera_ = SceneCamera{};
  viewport_ = ViewportTransform{};
}

bool RenderAgent::Init(const WindowHandle& window_handle) {
  renderer_ = std::make_unique<VulkanRenderer>(window_handle);
  return true;
}

RenderFrameResult RenderAgent::Tick(const Mesh& mesh, const TriangleOrientationState& orientation_state, const SceneCamera& camera, int highlighted_vertex, const RenderUiState& ui_state) {
  return renderer_->Draw(mesh, orientation_state, camera, highlighted_vertex, ui_state);
}

void RenderAgent::Destroy() {
  renderer_.reset();
}

IoBufferEndpoint RenderAgent::io_endpoint() {
  return IoBufferEndpoint{&inbound_io_packet_.protocol, &inbound_io_packet_.signal_buffer, &inbound_io_packet_.data_buffer};
}

void RenderAgent::ResolveIncomingIoBuffers() {
  PhysRunState run_state = PhysRunState::Pause;
  bool guide_enabled = true;
  if (DecodePhysRuntimeControlIoPacket(inbound_io_packet_, &run_state, &guide_enabled)) {
    SetPhysRunState(run_state);
    SetPhysGuideEnabled(guide_enabled);
    return;
  }

  std::vector<ValidationAction> actions;
  if (!DecodeValidationActionsIoPacket(inbound_io_packet_, &actions)) {
    return;
  }

  for (const ValidationAction& action : actions) {
    switch (action.kind) {
      case ValidationActionKind::Reset:
        SetPhysRunState(PhysRunState::Pause);
        SetPhysGuideEnabled(true);
        break;
      case ValidationActionKind::SetRunState:
        SetPhysRunState(action.run_state == ValidationPhysRunState::Run ? PhysRunState::Run : PhysRunState::Pause);
        break;
      case ValidationActionKind::SetGuideEnabled:
        SetPhysGuideEnabled(action.enabled);
        break;
      case ValidationActionKind::PhysStep:
        break;
    }
  }
}

InteractionMode RenderAgent::mode() const {
  return renderer_ ? renderer_->mode() : InteractionMode::Edit;
}

PhysRunState RenderAgent::phys_run_state() const {
  return renderer_ ? renderer_->phys_run_state() : PhysRunState::Pause;
}

bool RenderAgent::phys_guide_enabled() const {
  return renderer_ ? renderer_->phys_guide_enabled() : true;
}

void RenderAgent::SetPhysRunState(PhysRunState state) {
  if (renderer_) renderer_->SetPhysRunState(state);
}

void RenderAgent::SetPhysGuideEnabled(bool enabled) {
  if (renderer_) renderer_->SetPhysGuideEnabled(enabled);
}

SceneViewBounds RenderAgent::scene_view_bounds() const {
  return renderer_ ? renderer_->scene_view_bounds() : SceneViewBounds{};
}

VulkanComputeContextView RenderAgent::compute_context() const {
  return renderer_ ? renderer_->compute_context() : VulkanComputeContextView{};
}

bool ValidationAgent::Init(bool enabled) {
  enabled_ = enabled;
  if (enabled_) {
    return validation_layer_.Start();
  }
  return true;
}

std::vector<ValidationAction> ValidationAgent::Tick() {
  return validation_layer_.ConsumeActions();
}

void ValidationAgent::PublishFrame(const ValidationFrameSnapshot& snapshot) {
  if (enabled_) {
    validation_layer_.PublishFrame(snapshot);
  }
}

void ValidationAgent::Destroy() {
  validation_layer_.Stop();
  enabled_ = false;
}

IoBufferEndpoint ValidationAgent::io_endpoint() {
  return IoBufferEndpoint{&inbound_io_packet_.protocol, &inbound_io_packet_.signal_buffer, &inbound_io_packet_.data_buffer};
}

void ValidationAgent::ResolveIncomingIoBuffers() {}

void EditModeAgent::Init() {
  controller_ = std::make_unique<interaction_analysis::EditModeController>();
}

InteractionFrame EditModeAgent::Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel) {
  return controller_->Tick(mesh, viewport, camera, input, mouse_pixel);
}

void EditModeAgent::Destroy() {
  controller_.reset();
}

void GuideUiAgent::Init() {
  controller_ = std::make_unique<interaction_analysis::GuideUiController>();
}

GuideUiFrame GuideUiAgent::Tick(const Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel) {
  return controller_->Tick(mesh, viewport, camera, input, mouse_pixel);
}

void GuideUiAgent::Destroy() {
  if (controller_) {
    controller_->ClearSelection();
  }
  controller_.reset();
}

void PhysAgent::Init(const PhysManagerConfig& config, const VulkanComputeContextView& compute_context) {
  controller_ = std::make_unique<interaction_analysis::PhysModeController>();
  controller_->SetPhysicsConfig(config);
  controller_->SetGpuComputeContext(compute_context);
  previous_mode_ = InteractionMode::Edit;
}

void PhysAgent::TickModeTransition(InteractionMode mode, Mesh& mesh, const std::filesystem::path& snapshot_path) {
  if (!controller_) return;
  if (mode == InteractionMode::Phys && previous_mode_ != InteractionMode::Phys) {
    controller_->PhysInit(mesh);
    if (!std::filesystem::exists(snapshot_path)) {
      SaveMeshSnapshotFile(mesh, snapshot_path.string());
    }
  }
  previous_mode_ = mode;
}

void PhysAgent::SetSelectedGuideVertices(const std::vector<int>& vertices) {
  if (controller_) {
    controller_->SetSelectedGuideVertices(vertices);
  }
}

IoBufferEndpoint PhysAgent::io_endpoint() {
  return IoBufferEndpoint{&inbound_io_packet_.protocol, &inbound_io_packet_.signal_buffer, &inbound_io_packet_.data_buffer};
}

void PhysAgent::ResolveIncomingIoBuffers(Mesh& mesh) {
  if (!controller_) {
    return;
  }

  PhysRunState run_state = PhysRunState::Pause;
  bool guide_enabled = true;
  if (DecodePhysRuntimeControlIoPacket(inbound_io_packet_, &run_state, &guide_enabled)) {
    controller_->SetRunState(run_state);
    controller_->SetGuideEnabled(guide_enabled);
    return;
  }

  std::vector<ValidationAction> actions;
  if (DecodeValidationActionsIoPacket(inbound_io_packet_, &actions)) {
    for (const ValidationAction& action : actions) {
      switch (action.kind) {
        case ValidationActionKind::PhysStep: {
          const PhysRunState previous_run_state = controller_->run_state();
          controller_->SetRunState(PhysRunState::Pause);
          const uint32_t step_count = action.step_count == 0 ? 1u : action.step_count;
          for (uint32_t i = 0; i < step_count; ++i) {
            controller_->StepOnce(mesh);
          }
          controller_->SetRunState(previous_run_state);
          break;
        }
        case ValidationActionKind::Reset:
          controller_->Reset(mesh);
          controller_->SetRunState(PhysRunState::Pause);
          controller_->SetGuideEnabled(true);
          break;
        case ValidationActionKind::SetRunState:
          controller_->SetRunState(action.run_state == ValidationPhysRunState::Run ? PhysRunState::Run : PhysRunState::Pause);
          break;
        case ValidationActionKind::SetGuideEnabled:
          controller_->SetGuideEnabled(action.enabled);
          break;
      }
    }
    return;
  }

  if (inbound_io_packet_.protocol.name == kGuideUiPhysIoProtocolName) {
    IoBufferEndpoint controller_endpoint = controller_->io_endpoint();
    if (!controller_endpoint.valid()) {
      return;
    }
    *controller_endpoint.protocol = inbound_io_packet_.protocol;
    *controller_endpoint.signal_buffer = inbound_io_packet_.signal_buffer;
    *controller_endpoint.data_buffer = inbound_io_packet_.data_buffer;
    controller_->ResolveIncomingIoBuffers(mesh);
  }
}

InteractionFrame PhysAgent::Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  return controller_->Tick(mesh, viewport, camera, input, mouse_pixel, dt_seconds);
}

void PhysAgent::Destroy() {
  controller_.reset();
  previous_mode_ = InteractionMode::Edit;
}

void PhysAgent::SetRunState(PhysRunState run_state) {
  if (controller_) controller_->SetRunState(run_state);
}

void PhysAgent::SetGuideEnabled(bool enabled) {
  if (controller_) controller_->SetGuideEnabled(enabled);
}

void PhysAgent::SetGuideEditMode(GuideEditMode mode) {
  if (controller_) controller_->SetGuideEditMode(mode);
}

void PhysAgent::SetGuideVelocitySettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames) {
  if (controller_) controller_->SetGuideVelocitySettings(magnitude, delay_frames, duration_frames);
}

void PhysAgent::SetGuideForceSettings(float magnitude, uint32_t delay_frames, uint32_t duration_frames) {
  if (controller_) controller_->SetGuideForceSettings(magnitude, delay_frames, duration_frames);
}

PhysRunState PhysAgent::run_state() const {
  return controller_ ? controller_->run_state() : PhysRunState::Pause;
}

bool PhysAgent::guide_enabled() const {
  return controller_ ? controller_->guide_enabled() : true;
}

int PhysAgent::selected_velocity_guidance() const {
  return controller_ ? controller_->selected_velocity_guidance() : -1;
}

float PhysAgent::guide_velocity_magnitude() const {
  return controller_ ? controller_->guide_velocity_magnitude() : 1.0f;
}

uint32_t PhysAgent::guide_velocity_delay_frames() const {
  return controller_ ? controller_->guide_velocity_delay_frames() : 0u;
}

uint32_t PhysAgent::guide_velocity_duration_frames() const {
  return controller_ ? controller_->guide_velocity_duration_frames() : 1u;
}

float PhysAgent::guide_force_magnitude() const {
  return controller_ ? controller_->guide_force_magnitude() : 1.0f;
}

uint32_t PhysAgent::guide_force_delay_frames() const {
  return controller_ ? controller_->guide_force_delay_frames() : 0u;
}

uint32_t PhysAgent::guide_force_duration_frames() const {
  return controller_ ? controller_->guide_force_duration_frames() : 1u;
}

const std::vector<VelocityMatrix>& PhysAgent::total_velocities() const {
  static const std::vector<VelocityMatrix> kEmpty{};
  return controller_ ? controller_->total_velocities() : kEmpty;
}

const std::vector<VelocityMatrix>& PhysAgent::linear_velocities() const {
  static const std::vector<VelocityMatrix> kEmpty{};
  return controller_ ? controller_->linear_velocities() : kEmpty;
}

const std::vector<VelocityMatrix>& PhysAgent::angular_velocities() const {
  static const std::vector<VelocityMatrix> kEmpty{};
  return controller_ ? controller_->angular_velocities() : kEmpty;
}

const std::vector<PhysRecordedFrame>& PhysAgent::recorded_frames() const {
  static const std::vector<PhysRecordedFrame> kEmpty{};
  return controller_ ? controller_->recorded_frames() : kEmpty;
}

const std::vector<PhysGuideKeyframe>& PhysAgent::guide_keyframes() const {
  static const std::vector<PhysGuideKeyframe> kEmpty{};
  return controller_ ? controller_->guide_keyframes() : kEmpty;
}

const std::vector<VelocityGuidance>& PhysAgent::active_velocity_guidances() const {
  static const std::vector<VelocityGuidance> kEmpty{};
  return controller_ ? controller_->active_velocity_guidances() : kEmpty;
}

const std::vector<VelocityGuideVelocity>& PhysAgent::active_guide_velocities() const {
  static const std::vector<VelocityGuideVelocity> kEmpty{};
  return controller_ ? controller_->active_guide_velocities() : kEmpty;
}

const std::vector<VelocityGuideForce>& PhysAgent::active_guide_forces() const {
  static const std::vector<VelocityGuideForce> kEmpty{};
  return controller_ ? controller_->active_guide_forces() : kEmpty;
}

const std::vector<int>& PhysAgent::selected_guide_vertices() const {
  static const std::vector<int> kEmpty{};
  return controller_ ? controller_->selected_guide_vertices() : kEmpty;
}

GuideEditMode PhysAgent::guide_edit_mode() const {
  return controller_ ? controller_->guide_edit_mode() : GuideEditMode::Displacement;
}

int PhysAgent::current_frame_index() const {
  return controller_ ? controller_->current_frame_index() : 0;
}

PhysSolverKind PhysAgent::solver_kind() const {
  return controller_ ? controller_->solver_kind() : PhysSolverKind::Cpu;
}

const std::string& PhysAgent::algorithm_name() const {
  static const std::string kEmpty{};
  return controller_ ? controller_->algorithm_name() : kEmpty;
}

const GpuPhysicsDispatchDebugInfo& PhysAgent::gpu_dispatch_debug_info() const {
  static const GpuPhysicsDispatchDebugInfo kEmpty{};
  return controller_ ? controller_->gpu_dispatch_debug_info() : kEmpty;
}

void PhysAgent::StepOnce(Mesh& mesh) {
  if (controller_) controller_->StepOnce(mesh);
}

void PhysAgent::Reset(Mesh& mesh) {
  if (controller_) controller_->Reset(mesh);
}

void PhysAgent::CacheCurrentState() {
  if (controller_) controller_->CacheCurrentState();
}

void PhysAgent::RestoreRecordedFrame(Mesh& mesh, int state_index) {
  if (controller_) controller_->RestoreRecordedFrame(mesh, state_index);
}

void PhysAgent::SetRecordedFrameExpanded(int state_index, bool expanded) {
  if (controller_) controller_->SetRecordedFrameExpanded(state_index, expanded);
}

void PhysAgent::SetGuideKeyframeEnabled(int frame_index, bool enabled) {
  if (controller_) controller_->SetGuideKeyframeEnabled(frame_index, enabled);
}

void PhysAgent::SetGuideKeyframeExpandedByIndex(int index, bool expanded) {
  if (controller_) controller_->SetGuideKeyframeExpandedByIndex(index, expanded);
}

void IoBusAgent::Init() {}

void IoBusAgent::Destroy() {
  shared_bus_ = communication::SharedIoBus{};
  guide_ui_direct_line_ = communication::DedicatedIoLine{};
}

void IoBusAgent::BindSharedEndpoint(const std::string& endpoint_name, IoBufferEndpoint endpoint) {
  shared_bus_.BindEndpoint(endpoint_name, endpoint);
}

void IoBusAgent::BindGuideUiDirectLine(IoBufferEndpoint endpoint) {
  guide_ui_direct_line_.BindEndpoint(endpoint);
}

bool IoBusAgent::PublishToSharedEndpoint(const std::string& endpoint_name, const IoBufferPacket& packet) {
  return shared_bus_.WriteToEndpoint(endpoint_name, packet);
}

bool IoBusAgent::PublishGuideUiDirectLine(const IoBufferPacket& packet) {
  return guide_ui_direct_line_.Write(packet);
}

void TriangleAnalysisAgent::Init(const Mesh& reference_mesh) {
  reference_mesh_ = reference_mesh;
  state_ = TriangleOrientationState{};
  initialized_ = true;
}

void TriangleAnalysisAgent::Tick(const Mesh& mesh) {
  if (!initialized_ || !RunTriangleOrientationCpuAlgorithm(mesh, reference_mesh_, &state_)) {
    state_ = TriangleOrientationState{};
  }
}

void TriangleAnalysisAgent::Destroy() {
  reference_mesh_ = Mesh{};
  state_ = TriangleOrientationState{};
  initialized_ = false;
}

}  // namespace agents
