#include "debug_tool/debug_tool_backend_runtime.h"
#include "debug_tool/debug_tool_frontend_panel.h"

#include <SDL3/SDL_main.h>

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    (void)argc;
    (void)argv;

    DebugToolBackendRuntime runtime;
    DebugToolFrontendPanel ui_panel;
    if (!runtime.Init("debugTool", 1280, 720)) {
      throw std::runtime_error("DebugToolBackendRuntime init failed");
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
    std::cerr << "debugTool error: " << e.what() << '\n';
    return 1;
  }
}
