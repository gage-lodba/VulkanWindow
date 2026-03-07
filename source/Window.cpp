#include "Window.h"

#include <stdexcept>

Window::Window(int width, int height, const std::string &title, bool resizable)
    : window(nullptr),
      width(width),
      height(height),
      resizable(resizable),
      title(title) {
  initWindow();
}

Window::~Window() { cleanup(); }

void Window::initWindow() {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

  window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  if (!window) {
    throw std::runtime_error("Failed to create GLFW window");
  }

  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::framebufferResizeCallback(GLFWwindow *window, int width,
                                       int height) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
  self->width = width;
  self->height = height;
  self->framebufferResized = true;
}

void Window::cleanup() noexcept {
  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
}

auto Window::shouldClose() const noexcept -> bool {
  return glfwWindowShouldClose(window);
}

void Window::pollEvents() const noexcept { glfwPollEvents(); }
