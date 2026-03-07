#pragma once

#include <GLFW/glfw3.h>

#include <string>

class Window {
 public:
  Window(int width, int height, const std::string &title,
         bool resizable = true);
  ~Window();

  // Non-copyable, non-movable
  Window(const Window &) = delete;
  auto operator=(const Window &) -> Window & = delete;
  Window(Window &&) = delete;
  auto operator=(Window &&) -> Window & = delete;

  [[nodiscard]] auto shouldClose() const noexcept -> bool;
  void pollEvents() const noexcept;
  [[nodiscard]] auto getGLFWWindow() const noexcept -> GLFWwindow * {
    return window;
  }
  [[nodiscard]] auto wasResized() const noexcept -> bool {
    return framebufferResized;
  }
  void resetResizedFlag() noexcept { framebufferResized = false; }

  /// Framebuffer dimensions in pixels (may differ from window coordinates on
  /// HiDPI).
  [[nodiscard]] auto getFramebufferWidth() const noexcept -> int {
    return width;
  }
  [[nodiscard]] auto getFramebufferHeight() const noexcept -> int {
    return height;
  }

 private:
  void initWindow();
  void cleanup() noexcept;

  static void framebufferResizeCallback(GLFWwindow *window, int width,
                                        int height);

  GLFWwindow *window;

  int width;
  int height;
  bool framebufferResized{false};
  bool resizable;

  std::string title;
};
