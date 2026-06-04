#include "interact_ui/interact_ui_runtime.h"
#include "common_data/mesh.h"

#include <SDL3/SDL_main.h>

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    (void)argc;
    (void)argv;

    Mesh mesh = common_data::BuildDefaultTriangleMesh();

    InteractUiRuntime runtime;
    if (!runtime.Init(mesh, "Interact & UI", 1280, 720)) {
      throw std::runtime_error("InteractUiRuntime init failed");
    }

    while (runtime.Tick()) {
    }

    runtime.Destroy();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Mesh editor error: " << e.what() << '\n';
    return 1;
  }
}
