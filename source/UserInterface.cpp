#include "UserInterface.h"
#include <imgui.h>

UserInterface::UserInterface() noexcept {}

void UserInterface::render(int windowWidth, int windowHeight) const {
  renderMainWindow(windowWidth, windowHeight);
}

void UserInterface::renderMainWindow(int windowWidth, int windowHeight) const {
  ImGuiWindowFlags window_flags;
  window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoTitleBar;

  const char *Title = "Vulkan Window";
  ImVec2 textSize = ImGui::CalcTextSize(Title);

  const ImVec2 viewport = ImGui::GetMainViewport()->WorkSize;
  const float midX = (viewport.x / 2) - 200;
  const float midY = (viewport.y / 2) - 150;

  ImGui::SetNextWindowPos({midX, midY}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({400, 300}, ImGuiCond_FirstUseEver);

  ImGui::Begin(Title, nullptr, window_flags);

  ImVec2 windSize = ImGui::GetWindowSize();
  const float centerX = (windSize.x / 2) - (textSize.x / 2);
  const float centerY = (windSize.y / 2) - (textSize.y / 2);

  ImGui::SetCursorPos({centerX, centerY});
  ImGui::Text("%s", Title);

  ImGui::SetCursorPosY(windSize.y - 30);
  if (ImGui::Button("Reset window pos", {-1, -1})) {
    ImGui::SetWindowPos({midX, midY});
  }

  ImGui::End();
}
