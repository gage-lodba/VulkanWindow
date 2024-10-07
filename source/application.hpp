#pragma once

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

class Application {
public:
  Application();
  ~Application();

  void Initialize();
  void Uninitialize();

  void Run();
  virtual void ApplyTheme();
  virtual void Render();

  bool isRunning();

  GLFWwindow *GetWindowHandle() const { return m_WindowHandle; }

  static VkInstance GetInstance();
  static VkPhysicalDevice GetPhysicalDevice();
  static VkDevice GetDevice();

private:
  bool Running;

  GLFWwindow *m_WindowHandle;
};
