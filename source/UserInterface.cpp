#include "UserInterface.h"

#include <imgui.h>

void UserInterface::render() { renderMainWindow(); }

void UserInterface::renderMainWindow() {
  static constexpr float WINDOW_WIDTH = 400.0f;
  static constexpr float WINDOW_HEIGHT = 300.0f;

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoTitleBar;

  const char *title = "Vulkan Window";
  ImVec2 textSize = ImGui::CalcTextSize(title);

  const ImVec2 viewport = ImGui::GetMainViewport()->WorkSize;
  const float midX = (viewport.x - WINDOW_WIDTH) / 2.0f;
  const float midY = (viewport.y - WINDOW_HEIGHT) / 2.0f;

  ImGui::SetNextWindowPos({midX, midY}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({WINDOW_WIDTH, WINDOW_HEIGHT},
                           ImGuiCond_FirstUseEver);

  ImGui::Begin(title, nullptr, window_flags);

  ImVec2 windSize = ImGui::GetWindowSize();
  const float centerX = (windSize.x / 2) - (textSize.x / 2);
  const float centerY = (windSize.y / 2) - (textSize.y / 2);

  ImGui::SetCursorPos({centerX, centerY});
  ImGui::Text("%s", title);

  ImGui::SetCursorPosY(windSize.y - 30);
  if (ImGui::Button("Reset window pos", {-1, -1})) {
    ImGui::SetWindowPos({midX, midY});
  }

  ImGui::End();
}
