#include "Application.h"

#include "VulkanRenderer.h"
#include "Window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

Application::Application() : Application(DEFAULT_WIDTH, DEFAULT_HEIGHT) {}

Application::Application(int width, int height, bool resizable) {
  // Initialize GLFW before creating any windows
  initGLFW();

  // Create window first
  window = std::make_unique<Window>(width, height, "Vulkan Window", resizable);

  // Create renderer with the window's GLFW handle
  renderer = std::make_unique<VulkanRenderer>(window->getGLFWWindow());
}

Application::~Application() {
  // Ensure renderer is cleaned up before window
  renderer.reset();

  // Window will be destroyed automatically by unique_ptr
  window.reset();

  // Terminate GLFW after all windows are destroyed
  terminateGLFW();
}

void Application::initGLFW() {
  if (glfwRefCount++ == 0) {
    if (!glfwInit()) {
      --glfwRefCount;
      throw std::runtime_error("Failed to initialize GLFW");
    }
  }
}

void Application::terminateGLFW() {
  if (--glfwRefCount == 0) {
    glfwTerminate();
  }
}

void Application::setUICallback(std::function<void()> callback) {
  renderer->setUICallback(std::move(callback));
}

void Application::setStyleCallback(std::function<void()> callback) {
  renderer->setStyleCallback(std::move(callback));
}

void Application::run() {
  // Main application loop
  while (!window->shouldClose()) {
    // Process window events (keyboard, mouse, resize, etc.)
    window->pollEvents();

    // Forward resize notification to renderer
    if (window->wasResized()) {
      window->resetResizedFlag();
      renderer->notifyResized();
    }

    // Render a frame
    renderer->drawFrame();
  }
}