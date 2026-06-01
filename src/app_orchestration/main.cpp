#include "entity_interaction/entity_interaction_agents.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <filesystem>
#include <stdexcept>
#include <string>

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
    if (!runtime.Init(mesh, "Entity Interaction Editor", 1280, 720)) {
      throw std::runtime_error("EntityInteractionRuntime init failed");
    }

    std::string mesh_error;
    if (!runtime.LoadMeshFromFile(mesh_path.string(), &mesh_error)) {
      throw std::runtime_error(mesh_error.empty() ? "mesh binding failed" : mesh_error);
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
