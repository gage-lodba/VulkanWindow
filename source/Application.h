#pragma once
#include "VulkanRenderer.h"
#include "Window.h"

#include <memory>

class Application {
public:
  Application();
  ~Application();

  void run();

private:
  std::unique_ptr<Window> window;
  std::unique_ptr<VulkanRenderer> renderer;

  const int WIDTH = 800;
  const int HEIGHT = 600;
};
