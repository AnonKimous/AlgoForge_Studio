#include "interact_ui/interact_ui_runtime.h"
#include "interact_ui/interact_ui_panel.h"

#include <SDL3/SDL_main.h>

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    (void)argc;
    (void)argv;

    InteractUiRuntime runtime;
    InteractUiPanel ui_panel;
    if (!runtime.Init("Interact & UI", 1280, 720)) {
      throw std::runtime_error("InteractUiRuntime init failed");
    }
    runtime.runtime_environment().SetDrawCallback([&]() {
      ui_panel.Draw(runtime);
    });

    while (runtime.Tick()) {
    }

    ui_panel.Destroy();
    runtime.Destroy();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Editor error: " << e.what() << '\n';
    return 1;
  }
}
