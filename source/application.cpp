#include "Application.h"

Application::Application() {
  window = std::make_unique<Window>(WIDTH, HEIGHT, "Vulkan window");
  renderer = std::make_unique<VulkanRenderer>(window->getGLFWWindow());
}

Application::~Application() {
  if (renderer) {
    renderer->waitIdle();
  }
}

void Application::run() {
  while (!window->shouldClose()) {
    window->pollEvents();

    if (window->wasWindowResized()) {
      renderer->handleResize();
      window->resetWindowResizedFlag();
    }

    renderer->drawFrame();
  }
}
