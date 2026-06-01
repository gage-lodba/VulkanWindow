#include "Window.h"

#include <stdexcept>
#include <string>

Window::Window(int width, int height, std::string_view title, bool resizable) {
  initWindow(width, height, title, resizable);
}

Window::~Window() { cleanup(); }

void Window::initWindow(int width, int height, std::string_view title,
                        bool resizable) {
  // Reset hints to defaults so any prior state (e.g. a previous Window in the
  // process leaving GLFW_TRANSPARENT_FRAMEBUFFER set) doesn't leak in.
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
  // Explicit: under Wayland an opaque framebuffer is required for the
  // compositor to honor VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR reliably.
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);

  // string_view isn't guaranteed null-terminated; copy into a std::string for
  // glfwCreateWindow which takes a C string.
  const std::string titleStr(title);
  // width/height passed to glfwCreateWindow are in window coords; on HiDPI
  // the resulting framebuffer is larger.
  window = glfwCreateWindow(width, height, titleStr.c_str(), nullptr, nullptr);
  if (!window) {
    throw std::runtime_error("Failed to create GLFW window");
  }

  // Seed the cached framebuffer dimensions with actual pixel values so HiDPI
  // consumers get correct values before the first resize callback fires.
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

  // Install input thunks now, before ImGui's GLFW backend is initialised
  // (ImGui_ImplGlfw_InitForVulkan with install_callbacks=true snapshots the
  // current callbacks and chains them, so our thunks must already be in place
  // for chaining to work). The thunks themselves no-op when no std::function
  // is set, so consumers pay nothing for unused inputs.
  glfwSetKeyCallback(window, keyCallbackThunk);
  glfwSetCursorPosCallback(window, cursorPosCallbackThunk);
  glfwSetMouseButtonCallback(window, mouseButtonCallbackThunk);
  glfwSetScrollCallback(window, scrollCallbackThunk);
  glfwSetCharCallback(window, charCallbackThunk);
}

void Window::framebufferResizeCallback(GLFWwindow *window, int width,
                                       int height) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  if (!self) return;
  self->framebufferWidth = width;
  self->framebufferHeight = height;
  self->framebufferResized = true;
}

void Window::keyCallbackThunk(GLFWwindow *window, int key, int scancode,
                              int action, int mods) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  if (self && self->keyCallback) {
    self->keyCallback(key, scancode, action, mods);
  }
}

void Window::cursorPosCallbackThunk(GLFWwindow *window, double xpos,
                                    double ypos) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  if (self && self->cursorPosCallback) {
    self->cursorPosCallback(xpos, ypos);
  }
}

void Window::mouseButtonCallbackThunk(GLFWwindow *window, int button,
                                      int action, int mods) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  if (self && self->mouseButtonCallback) {
    self->mouseButtonCallback(button, action, mods);
  }
}

void Window::scrollCallbackThunk(GLFWwindow *window, double xoffset,
                                 double yoffset) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  if (self && self->scrollCallback) {
    self->scrollCallback(xoffset, yoffset);
  }
}

void Window::charCallbackThunk(GLFWwindow *window, unsigned int codepoint) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  if (self && self->charCallback) {
    self->charCallback(codepoint);
  }
}

void Window::cleanup() noexcept {
  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
}

auto Window::shouldClose() const noexcept -> bool {
  return glfwWindowShouldClose(window) != 0;
}

void Window::pollEvents() noexcept { glfwPollEvents(); }

void Window::waitEvents() noexcept { glfwWaitEvents(); }

void Window::setKeyCallback(KeyCallback callback) {
  keyCallback = std::move(callback);
}

void Window::setCursorPosCallback(CursorPosCallback callback) {
  cursorPosCallback = std::move(callback);
}

void Window::setMouseButtonCallback(MouseButtonCallback callback) {
  mouseButtonCallback = std::move(callback);
}

void Window::setScrollCallback(ScrollCallback callback) {
  scrollCallback = std::move(callback);
}

void Window::setCharCallback(CharCallback callback) {
  charCallback = std::move(callback);
}
