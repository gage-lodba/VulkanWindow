#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

#include "RendererTypes.h"  // PresentMode, SurfaceFormatPreference

class VulkanRenderer;
class Window;
class VulkanContext;
class Swapchain;
struct SwapchainRecreateInfo;

class Application {
 public:
  Application();
  /// `formatPreference` selects the swap-chain colour encoding. The default
  /// (Unorm) presents ImGui colours verbatim — correct for UI-centric apps;
  /// pass Srgb for linear-space 3D rendering (see SurfaceFormatPreference).
  /// `title` also names the per-app pipeline-cache directory.
  explicit Application(int width, int height, bool resizable = true,
                       std::string_view title = "Vulkan Window",
                       uint32_t framesInFlight = 2,
                       SurfaceFormatPreference formatPreference =
                           SurfaceFormatPreference::Unorm);

  ~Application();

  // Non-copyable, non-movable
  Application(const Application &) = delete;
  auto operator=(const Application &) -> Application & = delete;
  Application(Application &&) = delete;
  auto operator=(Application &&) -> Application & = delete;

  void run();

  /// Set a callback invoked each frame to issue ImGui draw calls.
  /// Must be called from the thread that constructed Application; not safe to
  /// invoke concurrently with run().
  void setUICallback(std::function<void()> callback);

  /// Set a callback invoked each frame inside the main render pass, before
  /// ImGui records its draw commands. Use this to draw your own geometry; the
  /// command buffer is already in a recording state with the render pass
  /// begun. `extent` is the current swap-chain extent in pixels. ImGui
  /// composites on top of whatever is rendered here.
  /// Must be called from the thread that constructed Application; not safe to
  /// invoke concurrently with run().
  void setRenderCallback(
      std::function<void(VkCommandBuffer, VkExtent2D)> callback);

  /// Set a callback to customise the ImGui style/theme.
  /// Replaces the built-in dark theme. Applied immediately.
  /// Must be called from the thread that constructed Application; not safe to
  /// invoke concurrently with run().
  void setStyleCallback(std::function<void()> callback);

  /// Set a callback that loads custom fonts into ImGui's atlas. Applied
  /// immediately, and re-applied automatically after a swap-chain rebuild that
  /// recreates the ImGui context (e.g. a surface-format change), so the fonts
  /// persist. Inside the callback add fonts with
  /// `ImGui::GetIO().Fonts->AddFontFromFileTTF(path, sizePx)` — Dear ImGui
  /// (>= 1.92) uploads the atlas lazily, so no manual texture build is needed.
  /// Must be called from the thread that constructed Application; not safe to
  /// invoke concurrently with run().
  void setFontCallback(std::function<void()> callback);

  /// Set a callback invoked after the swap-chain is rebuilt (resize, present-
  /// mode switch, surface-format change). Use it to recreate pipelines or
  /// resources that depend on the render pass, formats, or extent; the
  /// `SwapchainRecreateInfo` argument flags whether the format / image count
  /// changed and carries the new extent. The device is idle when it fires.
  /// Not called for the initial swap-chain — create those resources up front
  /// (see getContext() / getSwapchain()). Include "VulkanRenderer.h" for the
  /// SwapchainRecreateInfo definition.
  /// Must be called from the thread that constructed Application; not safe to
  /// invoke concurrently with run().
  void setSwapchainRecreatedCallback(
      std::function<void(const SwapchainRecreateInfo &)> callback);

  /// Input callbacks. Each replaces any previously set callback of the same
  /// kind; pass an empty std::function to clear. ImGui's GLFW backend chains
  /// alongside these, so the callback fires for events ImGui consumed too.
  /// Filter on `ImGui::GetIO()` `WantCaptureKeyboard` / `WantCaptureMouse`
  /// if the app should ignore events ImGui is using.
  void setKeyCallback(
      std::function<void(int key, int scancode, int action, int mods)>
          callback);
  void setCursorPosCallback(
      std::function<void(double xpos, double ypos)> callback);
  void setMouseButtonCallback(
      std::function<void(int button, int action, int mods)> callback);
  void setScrollCallback(
      std::function<void(double xoffset, double yoffset)> callback);
  void setCharCallback(std::function<void(unsigned int codepoint)> callback);

  /// Change the renderer's preferred present mode at runtime. Triggers a
  /// swap-chain rebuild on the next frame. The requested mode may fall back
  /// to vsync (FIFO) if the surface doesn't support it.
  void setPresentMode(PresentMode mode);
  [[nodiscard]] auto getPresentMode() const noexcept -> PresentMode;

  /// Set the colour cleared into the swap-chain image at the start of each
  /// frame. Components in [0, 1]. With the default Unorm surface they're
  /// presented verbatim (treat them as sRGB/display values, matching ImGui's
  /// colours); with an Srgb surface they're treated as linear and the GPU
  /// gamma-encodes them. Default is opaque black.
  void setClearColor(float r, float g, float b, float a = 1.0f) noexcept;

  // ---- Advanced: direct Vulkan interop ------------------------------------
  //
  // Read-only handles to the underlying Vulkan objects, so you can build your
  // own pipelines / buffers / images and record them from setRenderCallback
  // without forking the scaffold. Typical flow:
  //
  //   Application app;
  //   const VulkanContext &ctx = app.getContext();
  //   VkRenderPass pass = app.getSwapchain().renderPass;
  //   // Create a VkPipeline against ctx.device + pass + ctx.pipelineCache,
  //   // allocate buffers from ctx.allocator (VMA), and size any per-frame
  //   // data by app.getFramesInFlight().
  //   app.setRenderCallback([&](VkCommandBuffer cmd, VkExtent2D extent) {
  //     // Bind your pipeline and draw; index per-frame data by
  //     // app.getCurrentFrameIndex().
  //   });
  //   app.run();
  //
  // Include "VulkanContext.h" / "Swapchain.h" to read the members. The
  // VulkanContext reference is stable for the Application's lifetime. The
  // Swapchain object is stable too, but the handles *inside* it (renderPass,
  // imageFormat, depthFormat, extent, image views) are replaced when the
  // swap-chain is rebuilt on resize / present-mode / format change — re-read
  // them each frame and prefer dynamic viewport+scissor so a resize doesn't
  // force pipeline recreation (only a surface-format change does).

  [[nodiscard]] auto getContext() const noexcept -> const VulkanContext &;
  [[nodiscard]] auto getSwapchain() const noexcept -> const Swapchain &;

  /// Number of frames the renderer queues ahead of the GPU (set at
  /// construction). Size your per-frame resources by this and index them with
  /// getCurrentFrameIndex().
  [[nodiscard]] auto getFramesInFlight() const noexcept -> uint32_t;

  /// Index of the in-flight frame slot currently being recorded, in
  /// [0, getFramesInFlight()). Meaningful while a setRenderCallback invocation
  /// is running; use it to select this frame's slice of per-frame resources.
  [[nodiscard]] auto getCurrentFrameIndex() const noexcept -> uint32_t;

 private:
  static void initGLFW();
  static void terminateGLFW();

  // GLFW itself is not thread-safe — windowing calls (create/poll/destroy)
  // must run on the main thread. The mutex only serializes the init/terminate
  // refcount so that constructing/destroying Application instances back-to-back
  // (or from a thread that *only* manages lifetime) can't race the refcount.
  static inline std::mutex glfwInitMutex;
  static inline int glfwRefCount{0};

  std::unique_ptr<Window> window;
  std::unique_ptr<VulkanRenderer> renderer;

  static constexpr int DEFAULT_WIDTH = 800;
  static constexpr int DEFAULT_HEIGHT = 600;
};
