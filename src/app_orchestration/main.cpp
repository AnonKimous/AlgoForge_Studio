#include "entity_interaction/entity_interaction_agents.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

int main(int argc, char** argv) {
  try {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    if (!std::filesystem::exists(MESH_PATH)) {
      GenerateSubdividedTriangleMeshFile(MESH_PATH);
    }
    std::filesystem::path mesh_path = MESH_PATH;
    Mesh mesh = LoadMeshFile(mesh_path.string());

    EntityInteractionRuntime runtime;
    if (!runtime.Init(mesh, "Vulkan Mesh Vertex Editor", 1280, 720)) {
      throw std::runtime_error("EntityInteractionRuntime init failed");
    }

    auto camera_entity_handle = runtime.LoadEntity(entity_interaction::CreateCameraEntityInfo(mesh));
    if (camera_entity_handle == EntityInteractionRuntime::kInvalidEntityHandle) {
      throw std::runtime_error("camera entity load failed");
    }

    CreatePhysSolverInfo phys_solver_info{};
    phys_solver_info.solver_kind = PhysSolverKind::Cpu;
    phys_solver_info.algorithm_name = kCorotatedCpuAlgorithmName;
    phys_solver_info.run_algorithm_on_init = true;
    phys_solver_info.gpu_shader.shader_name = SHADER_PHYSICS_CONV_PATH;
    phys_solver_info.gpu_shader.shader_mask.assign(9, 1u);
    phys_solver_info.gpu_shader.shader_data.assign(9, 1.0f);
    if (phys_solver_info.solver_kind != PhysSolverKind::Cpu) {
      phys_solver_info.algorithm_name = kPhysicsConvolutionGpuAlgorithmName;
    }

    AlgorithmComplianceDescriptor algorithm_compliance_descriptor{};
    if (phys_solver_info.algorithm_name == kCorotatedCpuAlgorithmName) {
      algorithm_compliance_descriptor = CreateCorotatedCpuAlgorithmComplianceDescriptor(
        static_cast<uint32_t>(mesh.positions.size()),
        static_cast<uint32_t>(mesh.triangles.size()));
    } else if (phys_solver_info.algorithm_name == kPhysicsConvolutionGpuAlgorithmName) {
      algorithm_compliance_descriptor = CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor(
        static_cast<uint32_t>(mesh.positions.size()));
    }

    CreateEntityInfo physics_entity_info{};
    physics_entity_info.algorithm_name = phys_solver_info.algorithm_name;
    physics_entity_info.mounted_agent_name = "physics_agent";
    physics_entity_info.bound_resources = {"mesh", "physics_state", "compute_context"};
    physics_entity_info.compliance_packages.push_back(OrchestrationEntityAlgorithmPackageHandle{
      phys_solver_info.algorithm_name,
      nullptr});
    physics_entity_info.solver_config = phys_solver_info;
    physics_entity_info.compliance_descriptor = algorithm_compliance_descriptor;
    physics_entity_info.intervention_package = std::make_shared<OrchestrationEntityInterventionPackageHandle>();
    physics_entity_info.intervention_package->package_name = "physics_intervention";
    auto physics_entity_handle = runtime.LoadEntity(std::move(physics_entity_info));
    if (physics_entity_handle == EntityInteractionRuntime::kInvalidEntityHandle) {
      throw std::runtime_error("physics entity load failed");
    }

    while (runtime.Tick()) {
    }

    runtime.Destroy();
    SDL_Quit();
    return 0;
  } catch (const std::exception& e) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mesh editor error", e.what(), nullptr);
    SDL_Quit();
    return 1;
  }
}
