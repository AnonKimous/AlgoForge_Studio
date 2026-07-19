#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include "D:/relyingResourse/physx_dev/physx_5_5_0/install/vc17win64/PhysX/include/PxPhysicsAPI.h"
#include "D:/relyingResourse/physx_dev/physx_5_5_0/install/vc17win64/PhysX/include/gpu/PxGpu.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#pragma comment(lib, "D:\\relyingResourse\\physx_dev\\physx_5_5_0\\install\\vc17win64\\PhysX\\bin\\win.x86_64.vc143.md\\checked\\PhysX_64.lib")
#pragma comment(lib, "D:\\relyingResourse\\physx_dev\\physx_5_5_0\\install\\vc17win64\\PhysX\\bin\\win.x86_64.vc143.md\\checked\\PhysXCommon_64.lib")
#pragma comment(lib, "D:\\relyingResourse\\physx_dev\\physx_5_5_0\\install\\vc17win64\\PhysX\\bin\\win.x86_64.vc143.md\\checked\\PhysXFoundation_64.lib")

using namespace physx;

namespace {

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

PxFilterFlags PhysXLanceGpuFilterShader(
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

class PhysXLanceGpuState final {
 public:
  PhysXLanceGpuState()
      : foundation_(PxCreateFoundation(PX_PHYSICS_VERSION, allocator_, error_callback_)),
        cuda_context_(PxCreateCudaContextManager(*foundation_, cuda_desc_, nullptr, true)),
        physics_(PxCreatePhysics(PX_PHYSICS_VERSION, *foundation_, PxTolerancesScale(), true, nullptr)),
        material_(physics_->createMaterial(0.5f, 0.5f, 0.1f)),
        scene_(CreateScene()),
        solid_(CreateSolid()),
        lance_(CreateLance()),
        trace_("testData/physx_lance_cuda_trace.log", std::ios::trunc) {
    trace_ << "physx_lance_cuda.begin\n";
    trace_ << "cuda.context_valid=" << cuda_context_->contextIsValid()
            << " device=" << cuda_context_->getDeviceName() << "\n";
    trace_ << "scene.gpu_dynamics=1 broadphase=gpu solver=tgs\n";
    scene_->addActor(*solid_);
    scene_->addActor(*lance_);
  }

  ~PhysXLanceGpuState() {
    trace_ << "physx_lance_cuda.end ticks=" << tick_count_
            << " impact=" << impact_ << "\n";
    lance_->release();
    solid_->release();
    scene_->release();
    material_->release();
    physics_->release();
    cuda_context_->release();
    foundation_->release();
  }

  void Step(
      algorithm::AlgorithmContainerSet* algorithm_container_set,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      agent::AlgorithmPackageDebugState* debug_state) {
    scene_->simulate(1.0f / 60.0f);
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
    algorithm::AlgorithmContainer* scene_data =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "scene_data");
    algorithm::AlgorithmContainer* render_draw =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "render_draw");
    const float render_scene[12] = {
      300.0f + pose.p.x * 45.0f,
      240.0f + pose.p.y * 75.0f,
      0.0f,
      velocity.x * 75.0f,
      180.0f,
      320.0f,
      190.0f,
      40.0f,
      100.0f,
      impact_ ? 1.0f : 0.0f,
      0.0f,
      0.0f,
    };
    const uint32_t draw_command[4] = {4u, 2u, 0u, 0u};
    std::memcpy(scene_data->bytes.data(), render_scene, sizeof(render_scene));
    std::memcpy(render_draw->bytes.data(), draw_command, sizeof(draw_command));
      debug_state->signals.push_back(algomanager::algoscheduler::AdvancedAlgorithmDebugSignal{
      .name = "physx_lance_cuda.step",
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
    scene_desc.filterShader = PhysXLanceGpuFilterShader;
    scene_desc.cudaContextManager = cuda_context_;
    scene_desc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
    scene_desc.broadPhaseType = PxBroadPhaseType::eGPU;
    scene_desc.solverType = PxSolverType::eTGS;
    scene_desc.gpuMaxNumPartitions = 8u;
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
  PxCudaContextManagerDesc cuda_desc_{};
  PxFoundation* foundation_;
  PxCudaContextManager* cuda_context_;
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

class PhysXLanceCudaExecutor final : public agent::IAlgorithmCudaExecutor {
 public:
  bool ExecuteCudaAlgorithm(
      const agent::AgentTickContext& context,
      const algorithm::AlgorithmProfile& algorithm_profile,
      const AgentToAlgorithmSignal& agent_to_algorithm_signal,
      algorithm::AlgorithmContainerSet* algorithm_container_set,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    state_.Step(algorithm_container_set, algorithm_to_agent_signal, debug_state);
    return true;
  }

 private:
  PhysXLanceGpuState state_{};
};

void DestroyPhysXLanceCudaExecutor(agent::IAlgorithmCudaExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
    const algomanager::support::AlgorithmPluginRequest* request,
    algomanager::support::AlgorithmPluginBundle* out_bundle) {
  (void)request;
  assert(out_bundle && "Algorithm plugin bundle output must be valid.");
  out_bundle->Clear();
  out_bundle->jobs_symbol = false;
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = true;
  out_bundle->compatibility_symbol = false;
  out_bundle->reflector = false;
  out_bundle->intervention = true;
  out_bundle->cuda_executor = new PhysXLanceCudaExecutor();
  out_bundle->destroy_cuda_executor = &DestroyPhysXLanceCudaExecutor;
  return true;
}
