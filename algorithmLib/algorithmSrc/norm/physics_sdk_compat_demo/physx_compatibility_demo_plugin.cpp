#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#define PX_SIMD_DISABLED 1
#include "D:/relyingResourse/physx_dev/physx_5_5_0/install/vc17win64/PhysX/include/PxPhysicsAPI.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <windows.h>

namespace {

using namespace physx;

class PhysXErrorCallback final : public PxErrorCallback {
 public:
  void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override {
    (void)code;
    (void)message;
    (void)file;
    (void)line;
  }
};

class InlineCpuDispatcher final : public PxCpuDispatcher {
 public:
  void submitTask(PxBaseTask& task) override {
    task.run();
    task.release();
  }

  uint32_t getWorkerCount() const override {
    return 1u;
  }
};

PxFilterFlags PhysXDemoFilterShader(
    PxFilterObjectAttributes attributes0,
    PxFilterData filter_data0,
    PxFilterObjectAttributes attributes1,
    PxFilterData filter_data1,
    PxPairFlags& pair_flags,
    const void* constant_block,
    PxU32 constant_block_size) {
  (void)attributes0;
  (void)filter_data0;
  (void)attributes1;
  (void)filter_data1;
  (void)constant_block;
  (void)constant_block_size;
  pair_flags = PxPairFlag::eCONTACT_DEFAULT |
    PxPairFlag::eNOTIFY_TOUCH_FOUND |
    PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
    PxPairFlag::eNOTIFY_CONTACT_POINTS;
  return PxFilterFlag::eDEFAULT;
}

class PhysXContactCallback final : public PxSimulationEventCallback {
 public:
  void BeginStep() {
    has_contact_ = false;
    contact_point_ = PxVec3(0.0f);
    contact_normal_ = PxVec3(0.0f);
    contact_impulse_ = PxVec3(0.0f);
  }

  bool has_contact() const {
    return has_contact_;
  }

  PxVec3 contact_point() const {
    return contact_point_;
  }

  PxVec3 contact_normal() const {
    return contact_normal_;
  }

  PxVec3 contact_impulse() const {
    return contact_impulse_;
  }

  void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) override {
    (void)constraints;
    (void)count;
  }

  void onWake(PxActor** actors, PxU32 count) override {
    (void)actors;
    (void)count;
  }

  void onSleep(PxActor** actors, PxU32 count) override {
    (void)actors;
    (void)count;
  }

  void onContact(
      const PxContactPairHeader& pair_header,
      const PxContactPair* pairs,
      PxU32 pair_count) override {
    (void)pair_header;
    for (PxU32 pair_index = 0u; pair_index < pair_count; ++pair_index) {
      const PxContactPair& pair = pairs[pair_index];
      if (pair.contactCount == 0u) {
        continue;
      }
      std::vector<PxContactPairPoint> contacts(pair.contactCount);
      const PxU32 contact_count = pair.extractContacts(contacts.data(), pair.contactCount);
      for (PxU32 contact_index = 0u; contact_index < contact_count; ++contact_index) {
        contact_point_ += contacts[contact_index].position;
        contact_normal_ += contacts[contact_index].normal;
        contact_impulse_ += contacts[contact_index].impulse;
      }
      contact_point_ /= static_cast<PxReal>(contact_count);
      contact_normal_.normalize();
      has_contact_ = true;
      return;
    }
  }

  void onTrigger(PxTriggerPair* pairs, PxU32 count) override {
    (void)pairs;
    (void)count;
  }

  void onAdvance(
      const PxRigidBody* const* body_buffer,
      const PxTransform* pose_buffer,
      const PxU32 count) override {
    (void)body_buffer;
    (void)pose_buffer;
    (void)count;
  }

 private:
  bool has_contact_{false};
  PxVec3 contact_point_{0.0f};
  PxVec3 contact_normal_{0.0f};
  PxVec3 contact_impulse_{0.0f};
};

void AppendBodyState(
    std::vector<float>* vertices,
    float center_x,
    float center_y,
    float half_extent,
    float body_id,
    float scale_x,
    float scale_y,
    float angle_z) {
  const float state[] = {
    center_x,
    center_y,
    half_extent,
    body_id,
    scale_x,
    scale_y,
    angle_z};
  vertices->insert(vertices->end(), std::begin(state), std::end(state));
}

class PhysXRigidBodyState final {
 public:
  PhysXRigidBodyState()
      : trace_("testData/physics_sdk_compat_demo_trace.log", std::ios::trunc) {
    Initialize();
  }

  void Initialize() {
    common_module_ = LoadPhysXModule(L"PhysXCommon_64.dll");
    foundation_module_ = LoadPhysXModule(L"PhysXFoundation_64.dll");
    physics_module_ = LoadPhysXModule(L"PhysX_64.dll");
    foundation_ = CreateFoundation();
    physics_ = CreatePhysics();
    constexpr float material_hardness = 0.92f;
    const float elastic_coefficient = 0.20f + material_hardness * 0.70f;
    material_ = physics_->createMaterial(0.5f, 0.5f, elastic_coefficient);
    material_hardness_ = material_hardness;
    scene_ = CreateScene();
    ground_ = CreateGround();
    box_a_ = CreateBox(PxVec3(-1.5f, 3.5f, 0.0f), PxVec3(0.75f, 0.75f, 0.75f));
    box_b_ = CreateBox(PxVec3(1.5f, 5.5f, 0.0f), PxVec3(0.65f, 0.65f, 0.65f));
    ResetBodies();
    scene_->addActor(*ground_);
    trace_ << "ground.added\n" << std::flush;
    scene_->addActor(*box_a_);
    trace_ << "box_a.added\n" << std::flush;
    scene_->addActor(*box_b_);
    trace_ << "box_b.added\n" << std::flush;
    trace_ << "physics_sdk_compat_demo.begin\n";
  }

  ~PhysXRigidBodyState() {
    trace_ << "physics_sdk_compat_demo.end ticks=" << tick_count_ << "\n";
    box_b_->release();
    box_a_->release();
    ground_->release();
    scene_->release();
    material_->release();
    physics_->release();
    foundation_->release();
    FreeLibrary(physics_module_);
    FreeLibrary(foundation_module_);
    FreeLibrary(common_module_);
  }

  void Step(
      const agent::AlgorithmCompatibilityContainerWriter* container_writer,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      agent::AlgorithmPackageDebugState* debug_state) {
    constexpr float timestep = 1.0f / 60.0f;
    simulation_time_seconds_ += timestep;
    if (simulation_time_seconds_ >= 5.0f) {
      ResetBodies();
      simulation_time_seconds_ = 0.0f;
      trace_ << "reset period=5.0\n" << std::flush;
    }
    contact_callback_.BeginStep();
    trace_ << "step.begin\n" << std::flush;
    scene_->simulate(timestep);
    trace_ << "simulate.end\n" << std::flush;
    scene_->fetchResults(true);
    trace_ << "fetch.end\n" << std::flush;

    trace_ << "pose_a.begin\n" << std::flush;
    const PxTransform pose_a = box_a_->getGlobalPose();
    trace_ << "pose_a.end\n" << std::flush;
    const PxTransform pose_b = box_b_->getGlobalPose();
    trace_ << "pose_b.end\n" << std::flush;
    const PxVec3 velocity_a = box_a_->getLinearVelocity();
    trace_ << "velocity_a.end\n" << std::flush;
    const PxVec3 velocity_b = box_b_->getLinearVelocity();
    trace_ << "velocity_b.end\n" << std::flush;
    const PxVec3 angular_velocity_a = box_a_->getAngularVelocity();
    const PxVec3 angular_velocity_b = box_b_->getAngularVelocity();

    const float relative_speed = (velocity_a - velocity_b).magnitude();
    const float impact_strength = contact_callback_.has_contact()
      ? (std::min)(1.0f, contact_callback_.contact_impulse().magnitude() / 8.0f)
      : 0.0f;
    deformation_ = (std::max)(deformation_ * 0.82f, impact_strength);
    const float scale_x = 1.0f + deformation_ * 0.35f;
    const float scale_y = 1.0f - deformation_ * 0.28f;

    std::vector<float> vertices;
    vertices.reserve(2u * 7u);
    trace_ << "vertices.begin\n" << std::flush;
    AppendBodyState(
      &vertices,
      pose_a.p.x,
      pose_a.p.y,
      0.75f,
      1.0f,
      scale_x,
      scale_y,
      2.0f * std::atan2(pose_a.q.z, pose_a.q.w));
    trace_ << "box_a.state.end\n" << std::flush;
    AppendBodyState(
      &vertices,
      pose_b.p.x,
      pose_b.p.y,
      0.65f,
      2.0f,
      scale_x,
      scale_y,
      2.0f * std::atan2(pose_b.q.z, pose_b.q.w));
    trace_ << "box_b.state.end\n" << std::flush;

    trace_ << "containers.begin\n" << std::flush;
    container_writer->write_array_bytes(
      container_writer->context,
      "a1",
      vertices.data(),
      vertices.size() * sizeof(float));
    trace_ << "container.end\n" << std::flush;
    const uint32_t draw_command[] = {4u, 2u, 0u, 0u};
    container_writer->write_array_bytes(
      container_writer->context,
      "render_draw",
      draw_command,
      sizeof(draw_command));
    trace_ << "draw_command.end vertex_count=4 instance_count=2\n" << std::flush;

    ++tick_count_;
    trace_ << "tick=" << tick_count_
           << " box_a_y=" << pose_a.p.y
           << " box_b_y=" << pose_b.p.y
           << " box_a_vy=" << velocity_a.y
           << " box_b_vy=" << velocity_b.y
           << " box_a_wz=" << angular_velocity_a.z
           << " box_b_wz=" << angular_velocity_b.z
           << " relative_speed=" << relative_speed
           << " deformation=" << deformation_ << "\n";
    if (contact_callback_.has_contact()) {
      const PxVec3 relative_velocity = velocity_a - velocity_b;
      const PxReal reduced_mass =
        (box_a_->getMass() * box_b_->getMass()) /
        (box_a_->getMass() + box_b_->getMass());
      const PxVec3 relative_momentum = relative_velocity * reduced_mass;
      const PxVec3 contact_normal = contact_callback_.contact_normal();
      const PxReal incoming_speed = -(relative_velocity.dot(contact_normal));
      const PxReal collision_angle = relative_velocity.magnitude() > 0.0f
        ? std::acos((std::max)(-1.0f, (std::min)(1.0f, (-relative_velocity.getNormalized()).dot(contact_normal))))
        : 0.0f;
      const PxVec3 impulse = contact_callback_.contact_impulse();
      const PxVec3 impact_force_a = impulse / timestep;
      const PxVec3 impact_force_b = -impact_force_a;
      const PxVec3 angular_impulse_a = (contact_callback_.contact_point() - pose_a.p).cross(impulse);
      const PxVec3 angular_impulse_b = (contact_callback_.contact_point() - pose_b.p).cross(-impulse);
      const PxVec3 torque_a = (contact_callback_.contact_point() - pose_a.p).cross(impact_force_a);
      const PxVec3 torque_b = (contact_callback_.contact_point() - pose_b.p).cross(impact_force_b);
      trace_ << "contact.point=("
             << contact_callback_.contact_point().x << ","
             << contact_callback_.contact_point().y << ","
             << contact_callback_.contact_point().z << ") "
             << "normal=(" << contact_normal.x << "," << contact_normal.y << "," << contact_normal.z << ") "
             << "impulse=(" << impulse.x << "," << impulse.y << "," << impulse.z << ") "
             << "impulse_magnitude=" << impulse.magnitude() << " "
             << "force_a=(" << impact_force_a.x << "," << impact_force_a.y << "," << impact_force_a.z << ") "
             << "force_b=(" << impact_force_b.x << "," << impact_force_b.y << "," << impact_force_b.z << ") "
             << "relative_momentum=(" << relative_momentum.x << "," << relative_momentum.y << "," << relative_momentum.z << ") "
             << "incoming_speed=" << incoming_speed << " "
             << "collision_angle_degrees=" << collision_angle * 180.0f / 3.14159265358979323846f << " "
             << "angular_impulse_a=(" << angular_impulse_a.x << "," << angular_impulse_a.y << "," << angular_impulse_a.z << ") "
             << "angular_impulse_b=(" << angular_impulse_b.x << "," << angular_impulse_b.y << "," << angular_impulse_b.z << ") "
             << "torque_a=(" << torque_a.x << "," << torque_a.y << "," << torque_a.z << ") "
             << "torque_b=(" << torque_b.x << "," << torque_b.y << "," << torque_b.z << ") "
             << "angular_velocity_a=(" << angular_velocity_a.x << "," << angular_velocity_a.y << "," << angular_velocity_a.z << ") "
             << "angular_velocity_b=(" << angular_velocity_b.x << "," << angular_velocity_b.y << "," << angular_velocity_b.z << ") "
             << "material_hardness=" << material_hardness_ << " "
             << "elastic_coefficient=" << material_->getRestitution() << "\n";
    }
    (void)debug_state;
    algorithm_to_agent_signal->control_bits = static_cast<uint32_t>(tick_count_);
  }

 private:
  static std::wstring PluginDirectory() {
    wchar_t module_path[MAX_PATH]{};
    HMODULE module{};
    GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&PluginDirectory),
      &module);
    GetModuleFileNameW(module, module_path, MAX_PATH);
    std::wstring path(module_path);
    path.resize(path.find_last_of(L"\\/"));
    return path;
  }

  static HMODULE LoadPhysXModule(const wchar_t* name) {
    return LoadLibraryW((PluginDirectory() + L"\\" + name).c_str());
  }

  PxFoundation* CreateFoundation() {
    trace_ << "foundation.begin\n" << std::flush;
    using CreateFoundationFn = PxFoundation* (PX_CALL_CONV*)(PxU32, PxAllocatorCallback&, PxErrorCallback&);
    const auto create_foundation = reinterpret_cast<CreateFoundationFn>(GetProcAddress(foundation_module_, "PxCreateFoundation"));
    PxFoundation* foundation = create_foundation(PX_PHYSICS_VERSION, allocator_, error_callback_);
    trace_ << "foundation.end\n" << std::flush;
    return foundation;
  }

  PxPhysics* CreatePhysics() {
    trace_ << "physics.begin\n" << std::flush;
    using CreatePhysicsFn = PxPhysics* (PX_CALL_CONV*)(PxU32, PxFoundation&, const PxTolerancesScale&, bool, PxPvd*, PxOmniPvd*);
    const auto create_physics = reinterpret_cast<CreatePhysicsFn>(GetProcAddress(physics_module_, "PxCreatePhysics"));
    PxPhysics* physics = create_physics(PX_PHYSICS_VERSION, *foundation_, PxTolerancesScale(), false, nullptr, nullptr);
    trace_ << "physics.end\n" << std::flush;
    return physics;
  }

  PxScene* CreateScene() {
    trace_ << "scene.begin\n" << std::flush;
    PxSceneDesc scene_desc(physics_->getTolerancesScale());
    scene_desc.gravity = PxVec3(0.0f, 0.0f, 0.0f);
    scene_desc.cpuDispatcher = &dispatcher_;
    scene_desc.filterShader = PhysXDemoFilterShader;
    scene_desc.simulationEventCallback = &contact_callback_;
    scene_desc.flags |= PxSceneFlag::eENABLE_CCD;
    PxScene* scene = physics_->createScene(scene_desc);
    trace_ << "scene.end\n" << std::flush;
    return scene;
  }

  PxRigidStatic* CreateGround() {
    trace_ << "ground.begin\n" << std::flush;
    PxRigidStatic* ground = physics_->createRigidStatic(PxTransform(PxVec3(0.0f, -1.25f, 0.0f)));
    PxShape* shape = physics_->createShape(PxBoxGeometry(5.0f, 0.25f, 0.5f), *material_, true);
    ground->attachShape(*shape);
    shape->release();
    trace_ << "ground.end\n" << std::flush;
    return ground;
  }

  PxRigidDynamic* CreateBox(const PxVec3& position, const PxVec3& half_extents) {
    trace_ << "box.begin\n" << std::flush;
    PxRigidDynamic* box = physics_->createRigidDynamic(PxTransform(position));
    PxShape* shape = physics_->createShape(PxBoxGeometry(half_extents), *material_, true);
    box->attachShape(*shape);
    shape->release();
    box->setMass(1.0f);
    box->setMassSpaceInertiaTensor(PxVec3(1.0f, 1.0f, 1.0f));
    box->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
    trace_ << "box.end\n" << std::flush;
    return box;
  }

  void ResetBodies() {
    std::uniform_real_distribution<float> collision_x(-0.75f, 0.75f);
    std::uniform_real_distribution<float> collision_y(2.75f, 4.25f);
    std::uniform_real_distribution<float> start_distance(1.45f, 1.65f);
    std::uniform_real_distribution<float> time_to_collision(0.16f, 0.24f);
    std::uniform_real_distribution<float> impact_offset_magnitude(0.22f, 0.38f);
    const PxVec3 collision_point(collision_x(random_engine_), collision_y(random_engine_), 0.0f);
    const float side = (random_engine_() & 1u) == 0u ? -1.0f : 1.0f;
    const PxVec3 direction(side, 0.0f, 0.0f);
    const PxVec3 tangent(0.0f, 1.0f, 0.0f);
    const float distance_a = start_distance(random_engine_);
    const float distance_b = start_distance(random_engine_);
    const float collision_time = time_to_collision(random_engine_);
    const float impact_offset = impact_offset_magnitude(random_engine_);
    const float signed_impact_offset = (random_engine_() & 1u) == 0u
      ? impact_offset
      : -impact_offset;
    const PxVec3 position_a = collision_point + direction * distance_a + tangent * signed_impact_offset;
    const PxVec3 position_b = collision_point - direction * distance_b - tangent * signed_impact_offset;
    const PxVec3 velocity_a = -direction * ((distance_a - 0.75f) / collision_time);
    const PxVec3 velocity_b = direction * ((distance_b - 0.65f) / collision_time);
    box_a_->setGlobalPose(PxTransform(position_a));
    box_b_->setGlobalPose(PxTransform(position_b));
    box_a_->setLinearVelocity(velocity_a);
    box_b_->setLinearVelocity(velocity_b);
    box_a_->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
    box_b_->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
    box_a_->wakeUp();
    box_b_->wakeUp();
    deformation_ = 0.0f;
    trace_ << "reset collision_point=(" << collision_point.x << "," << collision_point.y << ")"
           << " time=" << collision_time
           << " position_a=(" << position_a.x << "," << position_a.y << ")"
           << " position_b=(" << position_b.x << "," << position_b.y << ")"
           << " impact_offset=" << signed_impact_offset
           << " velocity_a=(" << velocity_a.x << "," << velocity_a.y << ")"
           << " velocity_b=(" << velocity_b.x << "," << velocity_b.y << ")\n";
  }

  std::ofstream trace_;
  HMODULE common_module_;
  HMODULE foundation_module_;
  HMODULE physics_module_;
  PxDefaultAllocator allocator_{};
  PhysXErrorCallback error_callback_{};
  PhysXContactCallback contact_callback_{};
  PxFoundation* foundation_;
  PxPhysics* physics_;
  InlineCpuDispatcher dispatcher_{};
  PxMaterial* material_;
  PxScene* scene_;
  PxRigidStatic* ground_;
  PxRigidDynamic* box_a_;
  PxRigidDynamic* box_b_;
  uint64_t tick_count_{0u};
  float deformation_{0.0f};
  float material_hardness_{0.0f};
  float simulation_time_seconds_{0.0f};
  std::mt19937 random_engine_{0x50485958u};
};

class PhysXRigidBodyCompatibilityExecutor final : public agent::IAlgorithmCompatibilityExecutor {
 public:
  bool ExecuteCompatibleAlgorithm(
      const agent::AgentTickContext& context,
      const algorithm::AlgorithmProfile& algorithm_profile,
      const AgentToAlgorithmSignal& agent_to_algorithm_signal,
      const agent::AlgorithmCompatibilityContainerWriter* container_writer,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    state_.Step(container_writer, algorithm_to_agent_signal, debug_state);
    return true;
  }

 private:
  PhysXRigidBodyState state_{};
};

void DestroyPhysXRigidBodyCompatibilityExecutor(agent::IAlgorithmCompatibilityExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
    const algomanager::support::AlgorithmPluginRequest* request,
    algomanager::support::AlgorithmPluginBundle* out_bundle) {
  (void)request;
  out_bundle->Clear();
  out_bundle->jobs_symbol = false;
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = false;
  out_bundle->compatibility_symbol = true;
  out_bundle->reflector = false;
  out_bundle->intervention = true;
  out_bundle->compatibility_executor = new PhysXRigidBodyCompatibilityExecutor();
  out_bundle->destroy_compatibility_executor = &DestroyPhysXRigidBodyCompatibilityExecutor;
  return true;
}
