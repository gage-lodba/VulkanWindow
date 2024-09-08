#include "application.hpp"

class VulkanWindow : public Application {
 public:
  virtual void Render() override {
    ImGuiWindowFlags window_flags;
    window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoTitleBar;

    const char* Title = "Vulkan window";
    ImVec2 textSize = ImGui::CalcTextSize(Title);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float midX = (viewport->WorkSize.x / 2) - 200;
    const float midY = (viewport->WorkSize.y / 2) - 150;

    ImGui::SetNextWindowPos({midX, midY}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({400, 300}, ImGuiCond_FirstUseEver);

    ImGui::Begin("Hello, world!", nullptr, window_flags);

    ImVec2 windSize = ImGui::GetWindowSize();
    const float centerX = (windSize.x / 2) - (textSize.x / 2);
    const float centerY = (windSize.y / 2) - (textSize.y / 2);

    ImGui::SetCursorPos({centerX, centerY});
    ImGui::Text(Title);

    ImGui::SetCursorPosY(windSize.y - 30);
    if (ImGui::Button("Reset window pos", {-1, -1})) {
      ImGui::SetWindowPos({midX, midY});
    }

    ImGui::End();
  }

  virtual void ApplyTheme() override {
    ImGuiStyle* style = &ImGui::GetStyle();
    style->WindowRounding = 8.f;
    style->FrameRounding = 8.f;

    style->Colors[ImGuiCol_WindowBg] = ImColor(24, 24, 24);
    style->Colors[ImGuiCol_Text] = ImColor(255, 255, 255);
    style->Colors[ImGuiCol_Button] = ImColor(255.f, 255.f, 255.f, 0.125f);
    style->Colors[ImGuiCol_ButtonHovered] = ImColor(255.f, 255.f, 255.f, 0.25f);
    style->Colors[ImGuiCol_ButtonActive] = ImColor(255.f, 255.f, 255.f, 0.5f);
  }
};

int main(int, char**) {
  VulkanWindow app;
  app.ApplyTheme();
  app.Run();
}
