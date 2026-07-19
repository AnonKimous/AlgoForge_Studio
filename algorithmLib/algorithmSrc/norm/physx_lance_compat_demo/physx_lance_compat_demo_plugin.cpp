#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <PxPhysicsAPI.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>

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

PxFilterFlags PhysXLanceFilterShader(
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
  pair_flags = PxPairFlag::eCONTACT_DEFAULT;
  return PxFilterFlag::eDEFAULT;
}

class PhysXLanceState final {
 public:
  PhysXLanceState()
      : foundation_(PxCreateFoundation(PX_PHYSICS_VERSION, allocator_, error_callback_)),
        physics_(PxCreatePhysics(PX_PHYSICS_VERSION, *foundation_, PxTolerancesScale(), true, nullptr)),
        material_(physics_->createMaterial(0.5f, 0.5f, 0.1f)),
        scene_(CreateScene()),
        solid_(CreateSolid()),
        lance_(CreateLance()),
        trace_("testData/physx_lance_compat_trace.log", std::ios::trunc) {
    scene_->addActor(*solid_);
    scene_->addActor(*lance_);
    trace_ << "physx_lance_compat.begin\n";
  }

  ~PhysXLanceState() {
    trace_ << "physx_lance_compat.end ticks=" << tick_count_ << " impact=" << impact_ << "\n";
    lance_->release();
    solid_->release();
    scene_->release();
    material_->release();
    physics_->release();
    foundation_->release();
  }

  void Step(algomanager::bridge::AlgorithmPackageDebugState* debug_state, AlgorithmToAgentSignal* algorithm_to_agent_signal) {
    constexpr float timestep = 1.0f / 60.0f;
    scene_->simulate(timestep);
    scene_->fetchResults(true);

    const PxTransform pose = lance_->getGlobalPose();
    const PxVec3 velocity = lance_->getLinearVelocity();
    impact_ = impact_ || pose.p.x >= -1.75f || velocity.x < 0.0f;
    ++tick_count_;

    trace_ << "tick=" << tick_count_
           << " x=" << pose.p.x
           << " y=" << pose.p.y
           << " z=" << pose.p.z
           << " vx=" << velocity.x
           << " impact=" << impact_ << "\n";
    debug_state->signals.push_back(algomanager::bridge::AdvancedAlgorithmDebugSignal{
      .name = "physx_lance_compat.step",
      .payload = "tick=" + std::to_string(tick_count_) +
        ",x=" + std::to_string(pose.p.x) +
        ",vx=" + std::to_string(velocity.x) +
        ",impact=" + std::to_string(impact_),
    });
    algorithm_to_agent_signal->control_bits = impact_ ? 1u : 0u;
  }

 private:
  PxScene* CreateScene() {
    PxSceneDesc scene_desc(physics_->getTolerancesScale());
    scene_desc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    scene_desc.cpuDispatcher = &dispatcher_;
    scene_desc.filterShader = PhysXLanceFilterShader;
    return physics_->createScene(scene_desc);
  }

  PxRigidStatic* CreateSolid() {
    PxRigidStatic* solid = physics_->createRigidStatic(PxTransform(PxVec3(0.0f, 0.0f, 0.0f)));
    PxShape* shape = physics_->createShape(PxBoxGeometry(0.5f, 1.0f, 1.0f), *material_, true);
    solid->attachShape(*shape);
    shape->release();
    return solid;
  }

  PxRigidDynamic* CreateLance() {
    PxRigidDynamic* lance = physics_->createRigidDynamic(PxTransform(PxVec3(-4.0f, 0.0f, 0.0f)));
    PxShape* shape = physics_->createShape(PxCapsuleGeometry(0.15f, 1.25f), *material_, true);
    lance->attachShape(*shape);
    shape->release();
    lance->setMass(8.0f);
    lance->setMassSpaceInertiaTensor(PxVec3(1.0f, 1.0f, 1.0f));
    lance->setLinearVelocity(PxVec3(12.0f, 0.0f, 0.0f));
    return lance;
  }

  PxDefaultAllocator allocator_{};
  PhysXErrorCallback error_callback_{};
  PxFoundation* foundation_;
  PxPhysics* physics_;
  InlineCpuDispatcher dispatcher_{};
  PxMaterial* material_;
  PxScene* scene_;
  PxRigidStatic* solid_;
  PxRigidDynamic* lance_;
  std::ofstream trace_;
  uint64_t tick_count_{0u};
  bool impact_{false};
};

class PhysXLanceCompatibilityExecutor final : public algomanager::bridge::IAlgorithmCompatibilityExecutor {
 public:
  bool ExecuteCompatibleAlgorithm(
      const algomanager::bridge::AgentTickContext& context,
      const algorithm::AlgorithmProfile& algorithm_profile,
      const AgentToAlgorithmSignal& agent_to_algorithm_signal,
      const algomanager::bridge::AlgorithmCompatibilityContainerWriter* container_writer,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      algomanager::bridge::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    (void)container_writer;
    state_.Step(debug_state, algorithm_to_agent_signal);
    return true;
  }

 private:
  PhysXLanceState state_{};
};

void DestroyPhysXLanceCompatibilityExecutor(algomanager::bridge::IAlgorithmCompatibilityExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  (void)request;
  assert(out_bundle && "Algorithm plugin bundle output must be valid.");
  out_bundle->Clear();
  out_bundle->jobs_symbol = false;
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = false;
  out_bundle->compatibility_symbol = true;
  out_bundle->reflector = false;
  out_bundle->intervention = true;
  out_bundle->compatibility_executor = new PhysXLanceCompatibilityExecutor();
  out_bundle->destroy_compatibility_executor = &DestroyPhysXLanceCompatibilityExecutor;
  return true;
}
