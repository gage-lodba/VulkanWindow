#include "VulkanSelectors.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace vkutil {

auto sanitizeCacheName(std::string_view name) -> std::string {
  std::string out;
  out.reserve(name.size());
  for (char ch : name) {
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                    ch == '.' || ch == ' ';
    out.push_back(ok ? ch : '_');
  }
  // Windows strips trailing dots/spaces from path components, so a name like
  // "App." or "v1.0 " wouldn't round-trip. Drop them; this also collapses "."
  // and ".." to empty, which falls back below.
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
    out.pop_back();
  }
  if (out.empty()) {
    return "VulkanWindow";
  }
  return out;
}

auto chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available,
                             SurfaceFormatPreference preference)
    -> VkSurfaceFormatKHR {
  // Prefer 32-bit BGRA then RGBA in the requested encoding, all in the standard
  // sRGB-nonlinear display colour space. UNORM is the default so ImGui's
  // (non-gamma-corrected) colours present verbatim; SRGB is the opt-in for apps
  // doing their own linear-space rendering.
  const std::array<VkFormat, 2> preferred =
      preference == SurfaceFormatPreference::Srgb
          ? std::array{VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}
          : std::array{VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};

  for (VkFormat want : preferred) {
    for (const auto &availableFormat : available) {
      if (availableFormat.format == want &&
          availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
      }
    }
  }
  return available[0];
}

auto chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &available,
                           PresentMode preferred) -> VkPresentModeKHR {
  const auto contains = [&](VkPresentModeKHR mode) -> bool {
    return std::ranges::find(available, mode) != available.end();
  };

  // FIFO is required by the Vulkan spec to be supported on every swap-chain,
  // but defensively confirm it's in the surface's list. A non-conforming
  // driver would otherwise fail later in vkCreateSwapchainKHR with a less
  // actionable error.
  if (preferred == PresentMode::Vsync) {
    if (contains(VK_PRESENT_MODE_FIFO_KHR)) return VK_PRESENT_MODE_FIFO_KHR;
    if (!available.empty()) return available[0];
    return VK_PRESENT_MODE_FIFO_KHR;  // last resort; createSwapchain will fail
  }

  const VkPresentModeKHR target = preferred == PresentMode::Mailbox
                                      ? VK_PRESENT_MODE_MAILBOX_KHR
                                      : VK_PRESENT_MODE_IMMEDIATE_KHR;
  if (contains(target)) return target;
  return VK_PRESENT_MODE_FIFO_KHR;
}

auto clampSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                     uint32_t windowWidth,
                     uint32_t windowHeight) -> VkExtent2D {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }

  VkExtent2D actualExtent = {.width = windowWidth, .height = windowHeight};
  actualExtent.width =
      std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                 capabilities.maxImageExtent.width);
  actualExtent.height =
      std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                 capabilities.maxImageExtent.height);
  return actualExtent;
}

}  // namespace vkutil
