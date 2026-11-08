#pragma once

/// Stateless demo UI — issues ImGui draw calls each frame.
class UserInterface {
 public:
  UserInterface() noexcept = default;

  void render();

 private:
  void renderDockSpace();
  void renderMainWindow();
  void renderInfoWindow();
};
