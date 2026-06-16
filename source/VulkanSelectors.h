#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <string_view>
#include <vector>

#include "RendererTypes.h"  // PresentMode, SurfaceFormatPreference

// Pure selection/derivation logic split out of VulkanContext / Swapchain so it
// can be unit-tested without a Vulkan instance, device, or window. These
// functions take plain data in and return plain data out — no Vulkan calls, no
// I/O, no global state.
namespace vkutil {

/// Reduce an app name to a filesystem-safe directory component: keeps
/// alphanumerics plus `-_. ` and maps everything else to '_', then strips
/// trailing dots/spaces (which Windows drops from path components). Never
/// returns empty or a dot-only name — falls back to "VulkanWindow".
[[nodiscard]] auto sanitizeCacheName(std::string_view name) -> std::string;

/// Pick a surface format for the requested encoding: prefers 32-bit BGRA then
/// RGBA in the requested UNORM/sRGB encoding, all in the sRGB-nonlinear colour
/// space; falls back to the first available format. `available` must be
/// non-empty (the caller guarantees the surface exposes at least one format).
[[nodiscard]] auto chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &available,
    SurfaceFormatPreference preference) -> VkSurfaceFormatKHR;

/// Pick a present mode for the requested preference, falling back to
/// VK_PRESENT_MODE_FIFO_KHR (spec-guaranteed) when the preferred mode isn't
/// available. Pure: emits no warning — the caller can compare the result to
/// detect (and report) a fallback.
[[nodiscard]] auto chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &available, PresentMode preferred)
    -> VkPresentModeKHR;

/// Resolve the swap-chain extent. Returns `capabilities.currentExtent` when the
/// surface dictates it (width != UINT32_MAX); otherwise clamps the supplied
/// framebuffer size to the surface's min/max image extent. The window size is
/// passed in (rather than queried) so this stays free of GLFW.
[[nodiscard]] auto clampSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                                   uint32_t windowWidth, uint32_t windowHeight)
    -> VkExtent2D;

}  // namespace vkutil
