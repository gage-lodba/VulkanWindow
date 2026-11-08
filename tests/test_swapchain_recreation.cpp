// Manual regression smoke test for swap-chain recreation and the ImGui-context
// rebuild path (B1: a rebuilt ImGuiManager whose backends were never finalised
// crashed the next frame). It needs a real window/display and GPU path, so it
// is deliberately NOT registered with ctest — the smoke CI job runs the binary
// directly.
//
// What it exercises:
//   * the documented runtime present-mode API (Mailbox at frame 10, Immediate
//     at frame 20, Vsync at frame 30); each switch forces a swap-chain rebuild,
//   * SwapchainRecreateInfo coherence: the image-count flag must match the
//     actual image count, and imguiContextRebuilt must match whether the ImGui
//     context was really replaced (tracked with a marker in ImGuiIO::UserData),
//   * the rebuilt ImGui backends must be fully live inside the callback (the
//     old B1 failure: backend data was still null there),
//   * the font callback must run exactly once per ImGui context (initial
//     context + one per rebuilt context), never twice for one context,
//   * the app keeps rendering after the rebuilds (ImGui draw calls every
//   frame).
//
// Run with e.g.
//   VULKANWINDOW_MAX_FRAMES=40 VULKANWINDOW_VALIDATION_ABORT=1 \
//     ./VulkanWindowSwapchainRecreation
// Expect exactly "recreation smoke OK" on stdout and exit status 0. If
// VULKANWINDOW_MAX_FRAMES is unset, the test closes the window after a fallback
// number of frames so a manual run still terminates.

#include <imgui.h>

#include <cstdio>
#include <cstdlib>

#include "Application.h"
#include "RendererTypes.h"
#include "Swapchain.h"
#include "SwapchainRecreateInfo.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace {

constexpr int kMailboxFrame = 10;
constexpr int kImmediateFrame = 20;
constexpr int kVsyncFrame = 30;
constexpr int kFallbackFrameCap = 40;
constexpr int kExpectedRebuilds = 3;

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

}  // namespace

auto main() -> int {
  try {
    Application app(800, 600, /*resizable=*/true, "VW swapchain recreation",
                    /*framesInFlight=*/2, SurfaceFormatPreference::Unorm);

    // ImGui context exists once Application is constructed; mark it so a later
    // rebuild can be detected from the callback.
    ImGui::GetIO().UserData = &contextMarker;

    int fontCallbackCalls = 0;
    int rebuilds = 0;
    int contextsRebuilt = 0;
    int frame = 0;
    size_t imageCount = app.getSwapchain().images.size();

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
        case kMailboxFrame:
          app.setPresentMode(PresentMode::Mailbox);
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
    if (rebuilds < kExpectedRebuilds) {
      fail("fewer swap-chain rebuilds than present-mode switches");
    }
    if (fontCallbackCalls != 1 + contextsRebuilt) {
      fail("font callback total does not match the number of ImGui contexts");
    }
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
    return 1;
  } catch (...) {
    std::fprintf(stderr, "recreation smoke unknown exception\n");
    return 1;
  }
}
