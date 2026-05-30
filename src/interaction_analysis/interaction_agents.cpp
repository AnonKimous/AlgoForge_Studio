#include "interaction_agents.h"

#include "algorithm/triangle_orientation_cpu_algorithm.h"

namespace interaction_analysis {

namespace {

void CopyEndpointToPacket(IoBufferPacket* destination, const IoBufferEndpoint& source) {
  if (!destination) {
    return;
  }
  if (!source.valid()) {
    *destination = IoBufferPacket{};
    return;
  }
  destination->protocol = *source.protocol;
  destination->signal_buffer = *source.signal_buffer;
  destination->data_buffer = *source.data_buffer;
}

void ConsumeEndpointToPacket(IoBufferPacket* destination, const IoBufferEndpoint& source) {
  CopyEndpointToPacket(destination, source);
  if (!source.valid()) {
    return;
  }
  *source.protocol = IoProtocolDescriptor{};
  source.signal_buffer->clear();
  source.data_buffer->clear();
}

}  // namespace

void TriangleAnalysisAgent::Init(const Mesh& reference_mesh) {
  reference_mesh_ = reference_mesh;
  state_ = TriangleOrientationState{};
  initialized_ = true;
}

void TriangleAnalysisAgent::Tick(const Mesh& mesh) {
  CodecManager codec{};
  TriangleOrientationCpuResourcePack resource_pack = codec.BuildTriangleOrientationCpuResourcePack(mesh, reference_mesh_);
  if (!initialized_ || !resource_pack.valid || !RunTriangleOrientationCpuAlgorithm(mesh, reference_mesh_, &state_)) {
    state_ = TriangleOrientationState{};
  }
}

void TriangleAnalysisAgent::Destroy() {
  reference_mesh_ = Mesh{};
  state_ = TriangleOrientationState{};
  initialized_ = false;
}

void EditModeAgent::Init() {
  controller_ = std::make_unique<EditModeController>();
}

InteractionFrame EditModeAgent::Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel) {
  return controller_->Tick(mesh, viewport, camera, input, mouse_pixel);
}

void EditModeAgent::Destroy() {
  controller_.reset();
}

void GuideUiAgent::Init() {
  controller_ = std::make_unique<GuideUiController>();
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

void PhysAgent::Init(
  const PhysSolverConfig& config,
  const VulkanComputeContextView& compute_context,
  const AlgorithmComplianceDescriptor& compliance_descriptor,
  IoBufferEndpoint guide_ui_io_endpoint) {
  controller_ = std::make_unique<PhysModeController>();
  controller_->SetPhysicsConfig(config);
  controller_->SetGpuComputeContext(compute_context);
  controller_->SetAlgorithmComplianceDescriptor(compliance_descriptor);
  guide_ui_io_endpoint_ = guide_ui_io_endpoint;
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

void PhysAgent::ResolveIncomingIoBuffers(Mesh& mesh) {
  if (!controller_) {
    return;
  }

  IoBufferPacket guide_ui_packet{};
  ConsumeEndpointToPacket(&guide_ui_packet, guide_ui_io_endpoint_);
  IoBufferEndpoint controller_endpoint = controller_->io_endpoint();
  if (!controller_endpoint.valid()) {
    return;
  }
  *controller_endpoint.protocol = guide_ui_packet.protocol;
  *controller_endpoint.signal_buffer = guide_ui_packet.signal_buffer;
  *controller_endpoint.data_buffer = guide_ui_packet.data_buffer;
  controller_->ResolveIncomingIoBuffers(mesh);
}

InteractionFrame PhysAgent::Tick(Mesh& mesh, const ViewportTransform& viewport, const SceneCamera& camera, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  return controller_->Tick(mesh, viewport, camera, input, mouse_pixel, dt_seconds);
}

void PhysAgent::Destroy() {
  controller_.reset();
  previous_mode_ = InteractionMode::Edit;
  guide_ui_io_endpoint_ = IoBufferEndpoint{};
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

}  // namespace interaction_analysis
