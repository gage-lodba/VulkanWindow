#pragma once

/// Stateless demo UI — issues ImGui draw calls each frame.
class UserInterface {
 public:
  UserInterface() noexcept = default;
  ~UserInterface() = default;

  void render();

 private:
  void renderMainWindow();
};
