#include <GLFW/glfw3.h>

#include <iostream>

#include "Application.h"
#include "UserInterface.h"

auto main() -> int {
  try {
    Application app;

    // ESC closes the window (GLFW has no default key-to-close binding).
    app.setKeyCallback([window = app.getWindow()](int key, int /*scancode*/,
                                                  int action,
                                                  int /*mods*/) -> void {
      if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
    });

    // Set up the default demo UI
    UserInterface ui;
    app.setUICallback([&ui]() -> void { ui.render(); });

    app.run();
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
