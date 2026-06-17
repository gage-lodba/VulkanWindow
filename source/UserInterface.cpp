#include "UserInterface.h"

#include <imgui.h>

#include <algorithm>

void UserInterface::render() {
  renderDockSpace();
  renderMainWindow();
  renderInfoWindow();
}

void UserInterface::renderDockSpace() {
#ifdef IMGUI_HAS_DOCK
  // Fullscreen invisible host window that hosts the central DockSpace. The
  // dockspace itself is the dropzone for child windows.
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  const ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground;

  ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
  ImGui::PopStyleVar(3);

  // PassthruCentralNode means the dockspace's central, undocked area is
  // transparent — anything the renderCallback drew shows through.
  const ImGuiID dockId = ImGui::GetID("VulkanWindowDockSpace");
  ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f),
                   ImGuiDockNodeFlags_PassthruCentralNode);

  ImGui::End();
#endif
}

void UserInterface::renderMainWindow() {
  constexpr float WINDOW_WIDTH = 400.0f;
  constexpr float WINDOW_HEIGHT = 300.0f;

  const char *title = "Vulkan Window";

  const ImVec2 viewport = ImGui::GetMainViewport()->WorkSize;
  const float midX = std::max(0.0f, (viewport.x - WINDOW_WIDTH) / 2.0f);
  const float midY = std::max(0.0f, (viewport.y - WINDOW_HEIGHT) / 2.0f);

  ImGui::SetNextWindowPos({midX, midY}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({WINDOW_WIDTH, WINDOW_HEIGHT},
                           ImGuiCond_FirstUseEver);

  ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse);

  const ImVec2 textSize = ImGui::CalcTextSize(title);
  const ImVec2 windSize = ImGui::GetWindowSize();
  const float centerX = (windSize.x / 2) - (textSize.x / 2);
  const float centerY = (windSize.y / 2) - (textSize.y / 2);

  ImGui::SetCursorPos({centerX, centerY});
  ImGui::Text("%s", title);

  // GetFrameHeightWithSpacing() includes trailing item spacing meant for the
  // *next* widget, which clips a few pixels off the bottom. Use frame height
  // + the bottom window padding so the button aligns to the actual edge.
  const float buttonHeight =
      ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;
  ImGui::SetCursorPosY(windSize.y - buttonHeight);
  if (ImGui::Button("Reset window pos", {-1, -1})) {
    ImGui::SetWindowPos({midX, midY});
  }

  ImGui::End();
}

void UserInterface::renderInfoWindow() {
  // A second window so the docking workflow has something to compose. Users
  // can drag this window into the main one to dock them side-by-side, or
  // (with multi-viewport on) drag it out of the main OS window entirely.
  ImGui::SetNextWindowSize({320.0f, 220.0f}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Info", nullptr, ImGuiWindowFlags_NoCollapse);

  ImGui::TextWrapped(
      "This window is dockable. Drag its title bar onto another window's "
      "tab strip or edge to dock; drag it out of the main viewport to "
      "detach it as its own OS window (requires the ImGui docking branch).");

  ImGui::Separator();

  const ImGuiIO &io = ImGui::GetIO();
  ImGui::Text("FPS: %.1f (%.2f ms/frame)", io.Framerate,
              1000.0f / io.Framerate);

#ifdef IMGUI_HAS_DOCK
  ImGui::Text("Docking: enabled");
#else
  ImGui::Text("Docking: not available (master-branch ImGui)");
#endif
#ifdef IMGUI_HAS_VIEWPORT
  ImGui::Text(
      "Viewports: %s",
      (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) ? "enabled" : "off");
#else
  ImGui::Text("Viewports: not available");
#endif

  ImGui::End();
}
