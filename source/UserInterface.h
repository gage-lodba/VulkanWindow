#pragma once

#include <vulkan/vulkan.h>

class UserInterface {
public:
  UserInterface() noexcept;
  ~UserInterface() = default;

  UserInterface(const UserInterface &) = delete;
  UserInterface &operator=(const UserInterface &) = delete;

  void render(int windowWidth, int windowHeight) const;

private:
  void renderMainWindow(int windowWidth, int windowHeight) const;
};
