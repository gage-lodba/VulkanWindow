#pragma once

#include <GLFW/glfw3.h>

#include <functional>
#include <string_view>

class Window {
 public:
  using KeyCallback =
      std::function<void(int key, int scancode, int action, int mods)>;
  using CursorPosCallback = std::function<void(double xpos, double ypos)>;
  using MouseButtonCallback =
      std::function<void(int button, int action, int mods)>;
  using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
  using CharCallback = std::function<void(unsigned int codepoint)>;

  Window(int width, int height, std::string_view title, bool resizable = true);
  ~Window();

  // Non-copyable, non-movable
  Window(const Window &) = delete;
  auto operator=(const Window &) -> Window & = delete;
  Window(Window &&) = delete;
  auto operator=(Window &&) -> Window & = delete;

  [[nodiscard]] auto shouldClose() const noexcept -> bool;
  // Not const: GLFW callbacks fire from here and mutate this Window via the
  // user-pointer (see framebufferResizeCallback).
  void pollEvents() noexcept;
  // Block until at least one event is available, then return.
  void waitEvents() noexcept;
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
    return framebufferWidth;
  }
  [[nodiscard]] auto getFramebufferHeight() const noexcept -> int {
    return framebufferHeight;
  }

  /// Input callbacks. Each replaces any previously set callback of the same
  /// kind; pass an empty std::function to clear. GLFW chains callbacks, so
  /// ImGui's installed handlers continue to fire alongside these — meaning the
  /// callback fires for events ImGui consumed too. Filter on `ImGui::GetIO()`
  /// `WantCaptureKeyboard` / `WantCaptureMouse` if the app should ignore
  /// events ImGui is using.
  void setKeyCallback(KeyCallback callback);
  void setCursorPosCallback(CursorPosCallback callback);
  void setMouseButtonCallback(MouseButtonCallback callback);
  void setScrollCallback(ScrollCallback callback);
  void setCharCallback(CharCallback callback);

 private:
  void initWindow(int width, int height, std::string_view title,
                  bool resizable);
  void cleanup() noexcept;

  static void framebufferResizeCallback(GLFWwindow *window, int width,
                                        int height);
  static void keyCallbackThunk(GLFWwindow *window, int key, int scancode,
                               int action, int mods);
  static void cursorPosCallbackThunk(GLFWwindow *window, double xpos,
                                     double ypos);
  static void mouseButtonCallbackThunk(GLFWwindow *window, int button,
                                       int action, int mods);
  static void scrollCallbackThunk(GLFWwindow *window, double xoffset,
                                  double yoffset);
  static void charCallbackThunk(GLFWwindow *window, unsigned int codepoint);

  GLFWwindow *window{nullptr};

  // Framebuffer pixel dimensions. Differ from the window-coord dimensions
  // requested at construction on HiDPI displays. Updated by GLFW's framebuffer
  // resize callback.
  int framebufferWidth{0};
  int framebufferHeight{0};
  bool framebufferResized{false};

  KeyCallback keyCallback;
  CursorPosCallback cursorPosCallback;
  MouseButtonCallback mouseButtonCallback;
  ScrollCallback scrollCallback;
  CharCallback charCallback;
};
