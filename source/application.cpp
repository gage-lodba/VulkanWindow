#include "Application.h"

Application::Application() : Application(DEFAULT_WIDTH, DEFAULT_HEIGHT) {}

Application::Application(int width, int height) {
  // Create window first
  window = std::make_unique<Window>(width, height, "Vulkan Window");

  // Create renderer with the window's GLFW handle
  renderer = std::make_unique<VulkanRenderer>(window->getGLFWWindow());
}

Application::~Application() {
  // Ensure renderer is cleaned up before window
  if (renderer) {
    renderer->waitIdle();
    renderer.reset();
  }

  // Window will be destroyed automatically by unique_ptr
}

void Application::run() {
  // Main application loop
  while (!window->shouldClose()) {
    // Process window events (keyboard, mouse, resize, etc.)
    window->pollEvents();

    // Render a frame
    renderer->drawFrame();
  }

  // Wait for device to finish before cleanup
  renderer->waitIdle();
}