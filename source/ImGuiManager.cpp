#include "ImGuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <stdexcept>

#include "VulkanUtils.h"  // srgbToLinear

// PipelineInfoMain was introduced 2025-09-26 (Dear ImGui v1.92.2). Earlier
// releases use flat RenderPass/Subpass/MSAASamples fields on
// ImGui_ImplVulkan_InitInfo and won't compile against this manager.
static_assert(IMGUI_VERSION_NUM >= 19220,
              "Dear ImGui >= 1.92.2 is required (PipelineInfoMain).");

ImGuiManager::ImGuiManager(
    GLFWwindow *window, uint32_t apiVersion, VkInstance instance,
    VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily,
    VkQueue queue, VkRenderPass renderPass, VkPipelineCache pipelineCache,
    uint32_t minImageCount, uint32_t imageCount, uint32_t textureSlots,
    std::function<void()> styleCallback, bool linearizeStyleColors,
    std::function<void()> fontCallback)
    : styleCallback(std::move(styleCallback)),
      fontCallback(std::move(fontCallback)),
      linearizeStyleColors(linearizeStyleColors) {
  bool contextCreated = false;
  bool glfwInited = false;
  bool vulkanInited = false;

  try {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated = true;
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // Disable layout persistence (stateless UI)

#ifdef IMGUI_HAS_DOCK
    // Dear ImGui docking branch: enable docking + multi-viewport. Master-
    // branch ImGui (no IMGUI_HAS_DOCK) skips this entire block.
    //
    // ViewportsEnable spawns OS-level windows for ImGui windows dragged out
    // of the main viewport. The Vulkan + GLFW backends own all of that
    // plumbing internally — the renderer only has to call
    // ImGuiManager::renderPlatformWindows() each frame (handled by
    // VulkanRenderer::drawFrame between submit and present).
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif

    setupStyle();

    // install_callbacks=true makes the ImGui backend chain its callbacks with
    // whatever was registered before, so Window::framebufferResizeCallback
    // (installed during Window construction) keeps firing. If ImGui init is
    // ever moved earlier than Window setup, that chaining order flips and
    // resize handling silently breaks.
    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
      throw std::runtime_error("Failed to initialize ImGui GLFW backend");
    }
    glfwInited = true;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = apiVersion;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = queueFamily;
    initInfo.Queue = queue;
    initInfo.PipelineCache = pipelineCache;
    // Let the backend create and own a correctly-typed descriptor pool rather
    // than hand-rolling one. ImGui 1.92 split the texture descriptor from a
    // single COMBINED_IMAGE_SAMPLER into separate SAMPLED_IMAGE + SAMPLER
    // descriptors; delegating pool creation (via DescriptorPoolSize, with
    // DescriptorPool left null) keeps us correct across future backend
    // descriptor changes. DescriptorPoolSize is the SAMPLED_IMAGE budget:
    // `textureSlots` user textures + 1 for the font atlas, floored at the
    // backend's documented minimum so a small slot count can't trip its assert.
    initInfo.DescriptorPool = VK_NULL_HANDLE;
    initInfo.DescriptorPoolSize =
        std::max(textureSlots + 1,
                 static_cast<uint32_t>(
                     IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE));
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.MinImageCount = minImageCount;
    initInfo.ImageCount = imageCount;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // For secondary viewports (multi-viewport). Backend creates the swap-
    // chain + render pass per viewport; we just pin the sample count.
    initInfo.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
      throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }
    vulkanInited = true;

    // Load user fonts (if any). With Dear ImGui >= 1.92's dynamic atlas the
    // Vulkan backend uploads/refreshes the texture lazily each frame, so fonts
    // can be added here (or later via setFontCallback) with no explicit
    // ImGui_ImplVulkan_CreateFontsTexture call. `this->` because the parameter
    // of the same name was just moved into the member.
    if (this->fontCallback) {
      this->fontCallback();
    }
  } catch (...) {
    // ImGui_ImplVulkan_Shutdown destroys the backend-owned descriptor pool, so
    // no separate pool teardown is needed on the failure path.
    if (vulkanInited) ImGui_ImplVulkan_Shutdown();
    if (glfwInited) ImGui_ImplGlfw_Shutdown();
    if (contextCreated) ImGui::DestroyContext();
    throw;
  }
}

ImGuiManager::~ImGuiManager() {
  // ImGui_ImplVulkan_Shutdown also destroys the descriptor pool the backend
  // created from DescriptorPoolSize.
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void ImGuiManager::newFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiManager::renderPlatformWindows() {
#ifdef IMGUI_HAS_VIEWPORT
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    // Spawns/destroys/resizes platform windows for secondary viewports, then
    // dispatches each viewport's Renderer_RenderWindow + Platform_RenderWindow
    // callbacks installed by the Vulkan/GLFW backends. Each secondary viewport
    // submits and presents its own commands; we don't have to thread them
    // through our own queue.
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
#endif
}

void ImGuiManager::setMinImageCount(uint32_t minImageCount) {
  ImGui_ImplVulkan_SetMinImageCount(minImageCount);
}

void ImGuiManager::setStyleCallback(std::function<void()> callback) {
  styleCallback = std::move(callback);
  // Apply immediately so the new style takes effect right away
  if (styleCallback) {
    styleCallback();
  }
}

void ImGuiManager::setFontCallback(std::function<void()> callback) {
  fontCallback = std::move(callback);
  // Apply immediately; the Vulkan backend re-uploads the atlas on the next
  // frame (dynamic font atlas, Dear ImGui >= 1.92).
  if (fontCallback) {
    fontCallback();
  }
}

void ImGuiManager::setupStyle() {
  if (styleCallback) {
    styleCallback();
    return;
  }

  // Default built-in dark theme
  ImGui::StyleColorsDark();

  ImGuiStyle *style = &ImGui::GetStyle();
  style->WindowRounding = 8.f;
  style->FrameRounding = 8.f;
  style->WindowBorderSize = 0.f;

  style->Colors[ImGuiCol_WindowBg] = ImColor(24, 24, 24, 255);
  style->Colors[ImGuiCol_Text] = ImColor(255, 255, 255, 255);
  style->Colors[ImGuiCol_Button] = ImColor(1.0f, 1.0f, 1.0f, 0.125f);
  style->Colors[ImGuiCol_ButtonHovered] = ImColor(1.0f, 1.0f, 1.0f, 0.25f);
  style->Colors[ImGuiCol_ButtonActive] = ImColor(1.0f, 1.0f, 1.0f, 0.5f);

#ifdef IMGUI_HAS_VIEWPORT
  // When viewports are enabled an ImGui window dragged out becomes a real OS
  // window. Rounded corners look wrong against a rectangular OS frame, and a
  // translucent WindowBg blends with whatever's behind the host window. The
  // ImGui docs' recommended adjustment is to square the rounding and force
  // full opacity in that mode.
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style->WindowRounding = 0.f;
    style->Colors[ImGuiCol_WindowBg].w = 1.f;
  }
#endif

  // On an sRGB swap-chain the GPU applies a linear→sRGB encode on store. ImGui
  // authors its palette in sRGB, so without compensation every colour is
  // double-encoded and washes out. Convert the whole palette to linear here so
  // the hardware encode reproduces the authored colours. Consumer-pushed
  // ImGuiCol_* / PushStyleColor values aren't covered — apply vkutil::
  // srgbToLinear to those yourself (see SurfaceFormatPreference::Srgb).
  if (linearizeStyleColors) {
    for (ImVec4 &c : style->Colors) {
      c.x = vkutil::srgbToLinear(c.x);
      c.y = vkutil::srgbToLinear(c.y);
      c.z = vkutil::srgbToLinear(c.z);
      // Alpha is not gamma-encoded; leave it.
    }
  }
}
