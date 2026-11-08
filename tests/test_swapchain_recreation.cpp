// Manual regression smoke test for swap-chain recreation and the ImGui-context
// rebuild path (B1: a rebuilt ImGuiManager whose backends were never finalised
// crashed the next frame). It needs a real window/display and GPU path, so it
// is deliberately NOT registered with ctest — the smoke CI job runs the binary
// directly.
//
// What it exercises:
//   * window resizes at frames 5 and 15 (800x600 -> 1000x700 -> 800x600), which
//     force extent-changing rebuilds. This needs a platform whose clients can
//     resize their own windows (X11, Cocoa/macOS, Win32); on Wayland the
//     requests are skipped and the resize assertions are replaced by a note
//     (xdg-shell gives clients no way to resize themselves),
//   * the documented runtime present-mode API (Mailbox at frame 10, Immediate
//     at frame 20, Vsync at frame 30); each switch forces a swap-chain rebuild,
//   * SwapchainRecreateInfo coherence: the image-count flag must match the
//     actual image count, and imguiContextRebuilt must match whether the ImGui
//     context was really replaced (tracked with a marker in ImGuiIO::UserData),
//   * the rebuild callback must see the new extent reflected both in
//     `info.extent`, in the rebuilt swap-chain, and in the live framebuffer
//     size (allowing for a window manager that coalesces or ignores a resize —
//     at least one extent-changing rebuild is required),
//   * the rebuilt ImGui backends must be fully live inside the callback (the
//     old B1 failure: backend data was still null there),
//   * the font callback must run exactly once per ImGui context (initial
//     context + one per rebuilt context), never twice for one context,
//   * the app keeps rendering after the rebuilds (ImGui draw calls every
//     frame).
//
// Wayland note: xdg-shell gives clients no way to resize their own windows
// (glfwSetWindowSize only changes GLFW's local notion of the size, which the
// compositor then overrides). On Linux with an X display (native X11 or
// XWayland) the test therefore asks GLFW for the X11 backend before
// initialisation, and fails with a clear message if X11 support is unavailable;
// the resize assertions then run strictly. On a Wayland-only host the
// present-mode, flag, backend and font assertions still run strictly, and the
// test prints "recreation smoke note: Wayland cannot client-resize;
// extent-change assertions skipped". CI smoke runners are X11 (xvfb) or macOS.
//
// Run with VULKANWINDOW_MAX_FRAMES=40 and VULKANWINDOW_VALIDATION_ABORT=1 set:
//   ./VulkanWindowSwapchainRecreation
// Expect exactly "recreation smoke OK" on stdout and exit status 0. If
// VULKANWINDOW_MAX_FRAMES is unset, the test closes the window after a fallback
// number of frames so a manual run still terminates.

#include <imgui.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "Application.h"
#include "RendererTypes.h"
#include "Swapchain.h"
#include "SwapchainRecreateInfo.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace {

constexpr int kInitialWidth = 800;
constexpr int kInitialHeight = 600;
constexpr int kWideWidth = 1000;
constexpr int kWideHeight = 700;

constexpr int kFirstResizeFrame = 5;
constexpr int kSecondResizeFrame = 15;
constexpr int kMailboxFrame = 10;
constexpr int kImmediateFrame = 20;
constexpr int kVsyncFrame = 30;
constexpr int kFallbackFrameCap = 40;
// Three present-mode switches always force a rebuild. On platforms that can
// client-resize, at least one of the two resize requests must add one more
// rebuild (the window manager may coalesce them into a single extent change).
constexpr int kPresentModeRebuilds = 3;

// Marker stored in ImGuiIO::UserData so a recreated context (whose UserData is
// null again) can be told apart from the surviving one. Comparing
// ImGui::GetCurrentContext() pointers is not enough: the allocator may reuse
// the freed context's address.
int contextMarker = 0;

int failures = 0;

void fail(const char *what) {
  std::fprintf(stderr, "recreation smoke FAILED: %s\n", what);
  ++failures;
}

auto maxFramesProvided() -> bool {
  const char *env = std::getenv("VULKANWINDOW_MAX_FRAMES");
  return env != nullptr && env[0] != '\0';
}

// Only these platforms let a client resize its own window: X11, Cocoa (macOS)
// and Win32. Wayland's xdg-shell deliberately cannot, and other GLFW platforms
// (e.g. the null platform) have no window manager to ask.
auto platformSupportsClientResize(int platform) -> bool {
  return platform == GLFW_PLATFORM_X11 || platform == GLFW_PLATFORM_COCOA ||
         platform == GLFW_PLATFORM_WIN32;
}

// Resize requests are asynchronous and the present-mode switches later in this
// test make the frame loop uncapped — without waiting, the window system could
// respond after the whole 40-frame budget has already elapsed. Push the
// request, then pump events (bounded) until the window reports the requested
// size. Pumping here is safe: GLFW input callbacks only queue ImGui events for
// the next frame and set the Window resize flag that Application::run() picks
// up on its next iteration. Only called on platforms that can client-resize
// (see platformSupportsClientResize).
void requestWindowSize(GLFWwindow *window, int width, int height) {
  glfwSetWindowSize(window, width, height);
  for (int attempt = 0; attempt < 100; ++attempt) {
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    if (windowWidth == width && windowHeight == height) return;
    glfwWaitEventsTimeout(0.005);  // bounded: at most ~0.5 s total
  }
}

}  // namespace

auto main() -> int {
  bool forcedX11 = false;
  bool appConstructed = false;
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
#if defined(GLFW_PLATFORM_X11)
  // See the Wayland note above: force X11 when an X display exists so resize
  // requests are honoured (XWayland under a Wayland desktop). glfwInitHint is
  // documented to be called before glfwInit, which Application's ctor runs.
  if (std::getenv("DISPLAY") != nullptr) {
    if (glfwPlatformSupported(GLFW_PLATFORM_X11) == GLFW_FALSE) {
      std::fprintf(stderr,
                   "recreation smoke FAILED: DISPLAY is set but this GLFW "
                   "binary has no X11 support, so the window-resize path "
                   "cannot be exercised (run on X11/XWayland, or unset DISPLAY "
                   "to exercise the Wayland path)\n");
      return 1;
    }
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    forcedX11 = true;
  }
#endif
#endif

  try {
    Application app(kInitialWidth, kInitialHeight, /*resizable=*/true,
                    "VW swapchain recreation", /*framesInFlight=*/2,
                    SurfaceFormatPreference::Unorm);
    appConstructed = true;

    // Detect the platform actually in use after GLFW initialisation (X11 even
    // on a Wayland desktop if DISPLAY was set above). Only X11, Cocoa and Win32
    // can honour a client resize request, so only there are extent-change
    // rebuilds required.
    const int platform = glfwGetPlatform();
    const bool resizeSupported = platformSupportsClientResize(platform);
    const int expectedRebuilds =
        resizeSupported ? kPresentModeRebuilds + 1 : kPresentModeRebuilds;

    // ImGui context exists once Application is constructed; mark it so a later
    // rebuild can be detected from the callback.
    ImGui::GetIO().UserData = &contextMarker;

    int fontCallbackCalls = 0;
    int rebuilds = 0;
    int contextsRebuilt = 0;
    int extentChanges = 0;
    int frame = 0;
    size_t imageCount = app.getSwapchain().images.size();
    VkExtent2D lastExtent = app.getSwapchain().extent;

    // Runs once immediately for the initial context, then once per rebuilt
    // context — never twice for the same context.
    app.setFontCallback([&fontCallbackCalls]() { ++fontCallbackCalls; });
    if (fontCallbackCalls != 1) {
      fail("font callback did not run exactly once when registered");
    }

    app.setSwapchainRecreatedCallback([&](const SwapchainRecreateInfo &info) {
      ++rebuilds;
      const Swapchain &swapchain = app.getSwapchain();

      if (swapchain.renderPass == VK_NULL_HANDLE) {
        fail("renderPass is null inside the rebuild callback");
      }
      if (swapchain.images.empty()) {
        fail("swap-chain has no images inside the rebuild callback");
      }
      if (info.extent.width == 0 || info.extent.height == 0) {
        fail("rebuild callback reported a zero extent");
      }
      if (info.extent.width != swapchain.extent.width ||
          info.extent.height != swapchain.extent.height) {
        fail("reported extent disagrees with the new swap-chain extent");
      }

      // The extent is in framebuffer pixels, so it must agree with the live
      // framebuffer size too (this holds across HiDPI: both are pixel sizes).
      int fbWidth = 0;
      int fbHeight = 0;
      glfwGetFramebufferSize(app.getWindow(), &fbWidth, &fbHeight);
      if (fbWidth <= 0 || fbHeight <= 0) {
        fail("framebuffer has a non-positive size inside the rebuild callback");
      } else if (info.extent.width != static_cast<uint32_t>(fbWidth) ||
                 info.extent.height != static_cast<uint32_t>(fbHeight)) {
        fail("reported extent disagrees with the live framebuffer size");
      }

      // Track extent-changing rebuilds: the resize requests must produce at
      // least one of them (present-mode switches don't change the extent).
      if (info.extent.width != lastExtent.width ||
          info.extent.height != lastExtent.height) {
        ++extentChanges;
      }
      lastExtent = info.extent;

      const bool actualImageCountChanged =
          swapchain.images.size() != imageCount;
      if (actualImageCountChanged != info.imageCountChanged) {
        fail("imageCountChanged flag disagrees with the new image count");
      }
      if ((info.formatChanged || info.imageCountChanged) &&
          !info.imguiContextRebuilt) {
        fail("format/image-count change must report imguiContextRebuilt");
      }

      const bool markerPresent = ImGui::GetIO().UserData == &contextMarker;
      if (info.imguiContextRebuilt) {
        ++contextsRebuilt;
        if (markerPresent) {
          fail("imguiContextRebuilt is set but the ImGui context survived");
        }
        // The rebuilt context is unmarked until here.
        ImGui::GetIO().UserData = &contextMarker;
      } else if (!markerPresent) {
        fail("ImGui context was replaced but imguiContextRebuilt is false");
      }

      // B1 regression check: the rebuilt backends must already be initialised
      // when this callback runs (pre-fix they were not, and the next frame
      // crashed). This also holds for the initial context.
      const ImGuiIO &io = ImGui::GetIO();
      if (io.BackendPlatformUserData == nullptr) {
        fail("ImGui platform backend is not initialised inside the callback");
      }
      if (io.BackendRendererUserData == nullptr) {
        fail("ImGui renderer backend is not initialised inside the callback");
      }

      const int expectedFontCalls = 1 + contextsRebuilt;
      if (fontCallbackCalls != expectedFontCalls) {
        fail("font callback did not run exactly once per ImGui context");
      }

      imageCount = swapchain.images.size();
    });

    app.setUICallback([&]() {
      ++frame;
      switch (frame) {
        case kFirstResizeFrame:
          if (resizeSupported) {
            requestWindowSize(app.getWindow(), kWideWidth, kWideHeight);
          }
          break;
        case kMailboxFrame:
          app.setPresentMode(PresentMode::Mailbox);
          break;
        case kSecondResizeFrame:
          if (resizeSupported) {
            requestWindowSize(app.getWindow(), kInitialWidth, kInitialHeight);
          }
          break;
        case kImmediateFrame:
          app.setPresentMode(PresentMode::Immediate);
          break;
        case kVsyncFrame:
          app.setPresentMode(PresentMode::Vsync);
          break;
        default:
          break;
      }
      // Exercise the ImGui draw path every frame, including the frames after a
      // context rebuild.
      ImGui::Begin("recreation smoke");
      ImGui::Text("frame %d", frame);
      ImGui::End();
      if (!maxFramesProvided() && frame >= kFallbackFrameCap) {
        glfwSetWindowShouldClose(app.getWindow(), GLFW_TRUE);
      }
    });

    app.run();

    if (frame < kVsyncFrame) {
      fail("frame loop ended before every present-mode switch ran");
    }
    if (rebuilds < expectedRebuilds) {
      fail(
          "fewer rebuilds than the present-mode switches plus a window resize");
    }
    if (resizeSupported) {
      if (extentChanges == 0) {
        fail("window resizes never produced an extent-changing rebuild");
      }
    } else if (platform == GLFW_PLATFORM_WAYLAND) {
      std::fprintf(stderr,
                   "recreation smoke note: Wayland cannot client-resize; "
                   "extent-change assertions skipped\n");
    } else {
      std::fprintf(stderr,
                   "recreation smoke note: platform %d cannot client-resize; "
                   "extent-change assertions skipped\n",
                   platform);
    }
    if (fontCallbackCalls != 1 + contextsRebuilt) {
      fail("font callback total does not match the number of ImGui contexts");
    }
    std::fprintf(stderr,
                 "recreation smoke: frames=%d rebuilds=%d extentChanges=%d "
                 "contextsRebuilt=%d\n",
                 frame, rebuilds, extentChanges, contextsRebuilt);
    // Not a failure: on some surfaces (e.g. a software WSI) stepping the
    // present mode never changes the ImGui min image count, so the context
    // rebuild path is not exercised there. The live-display run must show
    // contextsRebuilt > 0.
    if (contextsRebuilt == 0) {
      std::fprintf(stderr,
                   "recreation smoke note: present-mode switches never "
                   "recreated the ImGui context on this surface\n");
    }
    if (failures != 0) {
      std::fprintf(stderr, "recreation smoke FAILED with %d problem(s)\n",
                   failures);
      return 1;
    }

    std::printf("recreation smoke OK\n");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "recreation smoke exception: %s\n", e.what());
    if (forcedX11 && !appConstructed) {
      std::fprintf(stderr,
                   "recreation smoke note: GLFW was asked for the X11 backend "
                   "because DISPLAY is set; check that X11/XWayland is "
                   "available\n");
    }
    return 1;
  } catch (...) {
    std::fprintf(stderr, "recreation smoke unknown exception\n");
    return 1;
  }
}
