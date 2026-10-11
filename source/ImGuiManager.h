#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <functional>

class ImGuiManager {
 public:
  /// `textureSlots` caps how many distinct textures ImGui can hold
  /// simultaneously (each ImGui_ImplVulkan_AddTexture consumes one slot). The
  /// backend-owned descriptor pool is sized with one extra slot for ImGui's
  /// own font atlas so the slot count maps 1:1 to user textures.
  /// `styleCallback`, if non-empty, replaces the built-in dark theme during
  /// construction so the default isn't applied first and then overwritten.
  /// `linearizeStyleColors` converts the built-in theme's palette from sRGB to
  /// linear (only affects the built-in theme, not a user `styleCallback`); set
  /// it when the swap-chain format is sRGB so colours aren't double-encoded.
  /// `fontCallback`, if non-empty, is invoked during construction to load fonts
  /// into the atlas (and again whenever the ImGui context is rebuilt).
  ImGuiManager(GLFWwindow *window, uint32_t apiVersion, VkInstance instance,
               VkPhysicalDevice physicalDevice, VkDevice device,
               uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass,
               VkPipelineCache pipelineCache, uint32_t minImageCount,
               uint32_t imageCount, uint32_t textureSlots = 100,
               std::function<void()> styleCallback = {},
               bool linearizeStyleColors = false,
               std::function<void()> fontCallback = {});
  ~ImGuiManager();

  // Delete copy constructor and assignment operator
  ImGuiManager(const ImGuiManager &) = delete;
  auto operator=(const ImGuiManager &) -> ImGuiManager & = delete;

  void newFrame();
  void render(VkCommandBuffer commandBuffer);

  /// Update and render ImGui's secondary platform windows (multi-viewport).
  /// Must be called AFTER the main viewport has been submitted to the GPU
  /// and BEFORE its present, on the same thread as drawFrame(). No-op when
  /// the docking branch isn't in use or `ImGuiConfigFlags_ViewportsEnable`
  /// isn't set — and this manager leaves that flag OFF by default because the
  /// vendored ImGui Vulkan backend emits a validation error on this path
  /// (ocornut/imgui#9496). Re-enable it by setting the flag from a style
  /// callback: the style is applied by finalizeInit() *before* the backends
  /// initialise, which is what makes it take effect (ImGui's GLFW backend
  /// latches ImGuiBackendFlags_PlatformHasViewports at init). Note it stays
  /// off on Wayland regardless — see deps/ImGui/backends/imgui_impl_glfw.cpp.
  void renderPlatformWindows();

  /// Inform the Vulkan backend that the swap-chain's min image count
  /// changed (e.g. after a surface reconfig). Must be called outside of an
  /// ImGui frame.
  ///
  /// @warning The ImGui Vulkan backend treats a *change* of this value as
  /// unsupported and fires IM_ASSERT(0), so it is only safe to pass the value
  /// the context was constructed with. The renderer never calls this — it
  /// rebuilds the ImGui context when the min image count changes instead.
  void setMinImageCount(uint32_t minImageCount);

  /// Set a callback to customise the ImGui style/theme.
  /// Called once immediately (and can be called again to re-apply).
  /// If no callback is set, the built-in dark theme is used.
  void setStyleCallback(std::function<void()> callback);

  /// Set a callback that loads fonts into ImGui's atlas. Called once
  /// immediately (and again whenever the ImGui context is rebuilt). Add fonts
  /// via `ImGui::GetIO().Fonts->AddFontFromFileTTF(...)` inside it; the Vulkan
  /// backend uploads the atlas lazily, so no manual texture build is needed.
  void setFontCallback(std::function<void()> callback);

  /// Initialise the GLFW + Vulkan backends. The constructor deliberately stops
  /// short of this: the app's style callback cannot exist until Application's
  /// constructor has returned, and ImGui's backends latch some state from
  /// io.ConfigFlags at init (notably ImGuiBackendFlags_PlatformHasViewports).
  /// Applying the style before init is therefore the only way a style callback
  /// can influence backend setup. Idempotent — safe to call again after a
  /// swap-chain rebuild has already re-initialised the manager.
  void finalizeInit();

 private:
  void setupStyle();
  /// GLFW + Vulkan backend init, split out of the constructor so finalizeInit()
  /// can apply the style first. Throws on failure, leaving the context intact.
  void initBackends();

  // Backend-init parameters, stored because the constructor no longer uses them
  // directly — finalizeInit() does. Prefixed so they can't shadow the
  // same-named setter parameters.
  GLFWwindow *initWindow{nullptr};
  uint32_t initApiVersion{0};
  VkInstance initInstance{VK_NULL_HANDLE};
  VkPhysicalDevice initPhysicalDevice{VK_NULL_HANDLE};
  VkDevice initDevice{VK_NULL_HANDLE};
  uint32_t initQueueFamily{0};
  VkQueue initQueue{VK_NULL_HANDLE};
  VkRenderPass initRenderPass{VK_NULL_HANDLE};
  VkPipelineCache initPipelineCache{VK_NULL_HANDLE};
  uint32_t initMinImageCount{0};
  uint32_t initImageCount{0};
  uint32_t initTextureSlots{0};

  std::function<void()> styleCallback;
  std::function<void()> fontCallback;
  // When true, setupStyle() converts the built-in theme's palette from sRGB to
  // linear so it isn't double-encoded on an sRGB swap-chain.
  bool linearizeStyleColors{false};
  // True once ImGui::CreateContext() has run; guards DestroyContext().
  bool contextCreated{false};
  // True once both backends are up; makes finalizeInit() idempotent.
  bool backendsInited{false};
};
