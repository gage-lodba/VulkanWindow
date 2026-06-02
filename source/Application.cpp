#include "Application.h"

#include "VulkanRenderer.h"
#include "Window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

Application::Application() : Application(DEFAULT_WIDTH, DEFAULT_HEIGHT) {}

Application::Application(int width, int height, bool resizable,
                         std::string_view title, uint32_t framesInFlight,
                         SurfaceFormatPreference formatPreference) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Application width/height must be > 0");
  }

  // Initialize GLFW before creating any windows
  initGLFW();

  // If Window or VulkanRenderer construction throws, our destructor will not
  // run, so we must release the GLFW refcount here to avoid leaking it.
  try {
    window = std::make_unique<Window>(width, height, title, resizable);
    // The title doubles as the per-app pipeline-cache namespace.
    renderer = std::make_unique<VulkanRenderer>(
        window->getGLFWWindow(), PresentMode::Vsync, framesInFlight,
        formatPreference, title);
  } catch (...) {
    renderer.reset();
    window.reset();
    terminateGLFW();
    throw;
  }
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
  std::scoped_lock lock(glfwInitMutex);
  if (glfwRefCount == 0) {
    if (!glfwInit()) {
      throw std::runtime_error("Failed to initialize GLFW");
    }
  }
  ++glfwRefCount;
}

void Application::terminateGLFW() {
  std::scoped_lock lock(glfwInitMutex);
  if (--glfwRefCount == 0) {
    glfwTerminate();
  }
}

void Application::setUICallback(std::function<void()> callback) {
  renderer->setUICallback(std::move(callback));
}

void Application::setRenderCallback(
    std::function<void(VkCommandBuffer, VkExtent2D)> callback) {
  renderer->setRenderCallback(std::move(callback));
}

void Application::setStyleCallback(std::function<void()> callback) {
  renderer->setStyleCallback(std::move(callback));
}

void Application::setFontCallback(std::function<void()> callback) {
  renderer->setFontCallback(std::move(callback));
}

void Application::setSwapchainRecreatedCallback(
    std::function<void(const SwapchainRecreateInfo &)> callback) {
  renderer->setSwapchainRecreatedCallback(std::move(callback));
}

void Application::setKeyCallback(
    std::function<void(int, int, int, int)> callback) {
  window->setKeyCallback(std::move(callback));
}

void Application::setCursorPosCallback(
    std::function<void(double, double)> callback) {
  window->setCursorPosCallback(std::move(callback));
}

void Application::setMouseButtonCallback(
    std::function<void(int, int, int)> callback) {
  window->setMouseButtonCallback(std::move(callback));
}

void Application::setScrollCallback(
    std::function<void(double, double)> callback) {
  window->setScrollCallback(std::move(callback));
}

void Application::setCharCallback(
    std::function<void(unsigned int)> callback) {
  window->setCharCallback(std::move(callback));
}

void Application::setPresentMode(PresentMode mode) {
  renderer->setPresentMode(mode);
}

auto Application::getPresentMode() const noexcept -> PresentMode {
  return renderer->getPresentMode();
}

void Application::setClearColor(float r, float g, float b, float a) noexcept {
  renderer->setClearColor(r, g, b, a);
}

auto Application::getContext() const noexcept -> const VulkanContext & {
  return renderer->getContext();
}

auto Application::getSwapchain() const noexcept -> const Swapchain & {
  return renderer->getSwapchain();
}

auto Application::getFramesInFlight() const noexcept -> uint32_t {
  return renderer->getFramesInFlight();
}

auto Application::getCurrentFrameIndex() const noexcept -> uint32_t {
  return renderer->getCurrentFrameIndex();
}

void Application::run() {
  // Optional frame cap for headless / CI / benchmarking runs, where there is
  // no one to close the window. 0 (the default) means run until the window is
  // closed. The CI smoke test sets this to render a fixed number of frames and
  // then exit cleanly with a zero status.
  uint64_t maxFrames = 0;
  if (const char *env = std::getenv("VULKANWINDOW_MAX_FRAMES");
      env != nullptr && env[0] != '\0') {
    maxFrames = std::strtoull(env, nullptr, 10);
  }
  uint64_t framesRendered = 0;

  // Main application loop
  while (!window->shouldClose()) {
    // Process window events first so cached width/height reflect the latest
    // framebuffer-resize callback before we decide whether to skip the frame.
    window->pollEvents();

    // While the window is minimized (0×0 framebuffer) there is nothing to
    // draw — block on events instead of spinning at full CPU.
    if (window->getFramebufferWidth() == 0 ||
        window->getFramebufferHeight() == 0) {
      window->waitEvents();
      continue;
    }

    // Forward resize notification to renderer
    if (window->wasResized()) {
      window->resetResizedFlag();
      renderer->notifyResized();
    }

    // Render a frame
    renderer->drawFrame();

    // Stop after the requested number of drawn frames (minimized frames, which
    // skip drawFrame above, don't count).
    if (maxFrames != 0 && ++framesRendered >= maxFrames) {
      break;
    }
  }
}