#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <string>

class Window {
public:
  Window(int width, int height, const std::string &title);
  ~Window();

  // Delete copy constructor and assignment operator
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;

  [[nodiscard]] bool shouldClose() const noexcept;
  void pollEvents() const noexcept;
  [[nodiscard]] GLFWwindow *getGLFWWindow() const noexcept { return window; }

  [[nodiscard]] int getWidth() const noexcept { return width; }
  [[nodiscard]] int getHeight() const noexcept { return height; }

  [[nodiscard]] bool wasWindowResized() const noexcept {
    return framebufferResized;
  }
  void resetWindowResizedFlag() noexcept { framebufferResized = false; }

private:
  void initWindow();
  void cleanup() noexcept;

  static void framebufferResizeCallback(GLFWwindow *window, int width,
                                        int height);

  GLFWwindow *window;
  int width;
  int height;
  std::string title;
  bool framebufferResized{false};
};
