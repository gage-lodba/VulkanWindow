#include "ImGuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <stdexcept>

#include "VulkanUtils.h"  // srgbToLinear

// PipelineInfoMain was introduced 2025-09-26 and shipped in Dear ImGui v1.92.4.
// Earlier releases use flat RenderPass/Subpass/MSAASamples fields on
// ImGui_ImplVulkan_InitInfo and won't compile against this manager.
static_assert(IMGUI_VERSION_NUM >= 19240,
              "Dear ImGui >= 1.92.4 is required (PipelineInfoMain).");

ImGuiManager::ImGuiManager(
    GLFWwindow *window, uint32_t apiVersion, VkInstance instance,
    VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily,
    VkQueue queue, VkRenderPass renderPass, VkPipelineCache pipelineCache,
    uint32_t minImageCount, uint32_t imageCount, uint32_t textureSlots,
    std::function<void()> styleCallback, bool linearizeStyleColors,
    std::function<void()> fontCallback)
    : initWindow(window),
      initApiVersion(apiVersion),
      initInstance(instance),
      initPhysicalDevice(physicalDevice),
      initDevice(device),
      initQueueFamily(queueFamily),
      initQueue(queue),
      initRenderPass(renderPass),
      initPipelineCache(pipelineCache),
      initMinImageCount(minImageCount),
      initImageCount(imageCount),
      initTextureSlots(textureSlots),
      styleCallback(std::move(styleCallback)),
      fontCallback(std::move(fontCallback)),
      linearizeStyleColors(linearizeStyleColors) {
  try {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated = true;
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // Disable layout persistence (stateless UI)

#ifdef IMGUI_HAS_DOCK
    // Dear ImGui docking branch: enable docking. Master-branch ImGui (no
    // IMGUI_HAS_DOCK) skips this entire block.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // ImGuiConfigFlags_ViewportsEnable (multi-viewport: real OS windows for
    // ImGui windows dragged out of the main viewport) is deliberately left
    // off. The vendored ImGui 1.92.9b Vulkan backend submits a layout
    // transition on swap-chain images it has never acquired every time it
    // creates or resizes one of its viewport swap chains
    // (ImGui_ImplVulkanH_CreateOrResizeWindow, deps/ImGui/backends/
    // imgui_impl_vulkan.cpp:1961-1987). The validation layer reports that as
    // an error ("performs a layout transition on presentable VkImage ... but
    // the image has not been acquired"), which aborts the process whenever
    // VULKANWINDOW_VALIDATION_ABORT is set. Upstream: ocornut/imgui#9496.
    //
    // An app that wants multi-viewport back (e.g. after patching or upgrading
    // the backend) must set the flag before finalizeInit() runs the backends —
    // from its style callback, which finalizeInit() invokes first. ImGui's GLFW
    // backend only advertises ImGuiBackendFlags_PlatformHasViewports if the
    // flag is already set when ImGui_ImplGlfw_InitForVulkan() is called, so
    // setting it after init is too late: the flag lands on the context but the
    // viewport plumbing is never registered.
#endif

    setupStyle();
  } catch (...) {
    if (contextCreated) ImGui::DestroyContext();
    throw;
  }
}

void ImGuiManager::initBackends() {
  bool glfwInited = false;
  bool vulkanInited = false;

  try {
    // install_callbacks=true makes the ImGui backend chain its callbacks with
    // whatever was registered before, so Window::framebufferResizeCallback
    // (installed during Window construction) keeps firing. If ImGui init is
    // ever moved earlier than Window setup, that chaining order flips and
    // resize handling silently breaks.
    if (!ImGui_ImplGlfw_InitForVulkan(initWindow, true)) {
      throw std::runtime_error("Failed to initialize ImGui GLFW backend");
    }
    glfwInited = true;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = initApiVersion;
    initInfo.Instance = initInstance;
    initInfo.PhysicalDevice = initPhysicalDevice;
    initInfo.Device = initDevice;
    initInfo.QueueFamily = initQueueFamily;
    initInfo.Queue = initQueue;
    initInfo.PipelineCache = initPipelineCache;
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
        std::max(initTextureSlots + 1,
                 static_cast<uint32_t>(
                     IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE));
    initInfo.PipelineInfoMain.RenderPass = initRenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.MinImageCount = initMinImageCount;
    initInfo.ImageCount = initImageCount;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
#ifdef IMGUI_HAS_DOCK
    // For secondary viewports (multi-viewport). Backend creates the swap-
    // chain + render pass per viewport; we just pin the sample count.
    // PipelineInfoForViewports only exists in the docking branch.
    initInfo.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
#endif
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
      throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }
    vulkanInited = true;

    // Load user fonts (if any). With Dear ImGui >= 1.92's dynamic atlas the
    // Vulkan backend uploads/refreshes the texture lazily each frame, so fonts
    // can be added here (or later via setFontCallback) with no explicit
    // ImGui_ImplVulkan_CreateFontsTexture call.
    if (fontCallback) {
      fontCallback();
    }
  } catch (...) {
    // ImGui_ImplVulkan_Shutdown destroys the backend-owned descriptor pool, so
    // no separate pool teardown is needed on the failure path.
    if (vulkanInited) ImGui_ImplVulkan_Shutdown();
    if (glfwInited) ImGui_ImplGlfw_Shutdown();
    throw;
  }

  backendsInited = true;
}

void ImGuiManager::finalizeInit() {
  if (backendsInited) return;

  // Apply the style (and therefore any flag a style callback sets) *before* the
  // backends are initialised. This is the whole point of splitting
  // initialisation in two: the style callback is supplied by the app, which
  // cannot run until Application's constructor has returned, so it must be
  // applied here rather than during construction.
  setupStyle();
  initBackends();
}

ImGuiManager::~ImGuiManager() {
  // Backends first (ImGui_ImplVulkan_Shutdown also destroys the descriptor pool
  // the backend created), then the context. Guarded so a manager whose
  // finalizeInit() never ran — or whose context creation threw — still tears
  // down cleanly.
  if (backendsInited) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
  }
  if (contextCreated) {
    ImGui::DestroyContext();
  }
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
#ifdef IMGUI_HAS_DOCK
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    // Spawns/destroys/resizes platform windows for secondary viewports, then
    // dispatches each viewport's Renderer_RenderWindow + Platform_RenderWindow
    // callbacks installed by the Vulkan/GLFW backends. Each secondary viewport
    // submits and presents its own commands; we don't have to thread them
    // through our own queue.
    // ViewportsEnable is off by default (see the constructor), so this is a
    // no-op unless the app opted in. Do NOT enable that flag just to exercise
    // this path: with the vendored ImGui 1.92.9b backend it emits a validation
    // error on every viewport-window creation or resize
    // (ocornut/imgui#9496).
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
#endif
}

void ImGuiManager::setMinImageCount(uint32_t minImageCount) {
  // WARNING: the ImGui Vulkan backend treats a *change* of this value as
  // unsupported and fires IM_ASSERT(0) ("FIXME-VIEWPORT: Unsupported. Need to
  // recreate all swap chains!", imgui_impl_vulkan.cpp). The renderer never
  // calls this — a changed min image count rebuilds the ImGui context instead
  // (see VulkanRenderer::recreateSwapChain). Only call it with the value the
  // context was constructed with.
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

#ifdef IMGUI_HAS_DOCK
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
