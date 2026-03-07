#pragma once
#include <atomic>
#include <functional>
#include <memory>

class VulkanRenderer;
class Window;

class Application {
 public:
  Application();
  Application(int width, int height, bool resizable = true);

  ~Application();

  // Non-copyable, non-movable
  Application(const Application &) = delete;
  auto operator=(const Application &) -> Application & = delete;
  Application(Application &&) = delete;
  auto operator=(Application &&) -> Application & = delete;

  void run();

  /// Set a callback invoked each frame to issue ImGui draw calls.
  void setUICallback(std::function<void()> callback);

  /// Set a callback to customise the ImGui style/theme.
  /// Replaces the built-in dark theme. Applied immediately.
  void setStyleCallback(std::function<void()> callback);

 private:
  static void initGLFW();
  static void terminateGLFW();

  static inline std::atomic<int> glfwRefCount{0};

  std::unique_ptr<Window> window;
  std::unique_ptr<VulkanRenderer> renderer;

  static constexpr int DEFAULT_WIDTH = 800;
  static constexpr int DEFAULT_HEIGHT = 600;
};
