#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "RendererTypes.h"
#include "VulkanSelectors.h"

// ---- sanitizeCacheName ------------------------------------------------------

TEST_CASE("sanitizeCacheName keeps safe characters verbatim") {
  CHECK(vkutil::sanitizeCacheName("My App 1.0") == "My App 1.0");
  CHECK(vkutil::sanitizeCacheName("name-with_dash") == "name-with_dash");
}

TEST_CASE("sanitizeCacheName maps unsafe characters to underscore") {
  CHECK(vkutil::sanitizeCacheName("a/b\\c:d") == "a_b_c_d");
  // Each control char becomes '_'; only trailing dots/spaces are stripped, so
  // the trailing underscore from '\n' is kept.
  CHECK(vkutil::sanitizeCacheName("tab\tnl\n") == "tab_nl_");
}

TEST_CASE("sanitizeCacheName strips trailing dots and spaces") {
  CHECK(vkutil::sanitizeCacheName("App.") == "App");
  CHECK(vkutil::sanitizeCacheName("v1.0 ") == "v1.0");
  CHECK(vkutil::sanitizeCacheName("trailing   ") == "trailing");
}

TEST_CASE("sanitizeCacheName falls back when the result would be empty") {
  CHECK(vkutil::sanitizeCacheName("") == "VulkanWindow");
  CHECK(vkutil::sanitizeCacheName(".") == "VulkanWindow");
  CHECK(vkutil::sanitizeCacheName("..") == "VulkanWindow");
  CHECK(vkutil::sanitizeCacheName("   ") == "VulkanWindow");
}

// ---- chooseSwapSurfaceFormat ------------------------------------------------

namespace {
auto fmt(VkFormat f, VkColorSpaceKHR cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    -> VkSurfaceFormatKHR {
  return VkSurfaceFormatKHR{f, cs};
}
}  // namespace

TEST_CASE("chooseSwapSurfaceFormat prefers BGRA UNORM by default") {
  std::vector<VkSurfaceFormatKHR> avail = {fmt(VK_FORMAT_R8G8B8A8_UNORM),
                                           fmt(VK_FORMAT_B8G8R8A8_UNORM)};
  auto chosen =
      vkutil::chooseSwapSurfaceFormat(avail, SurfaceFormatPreference::Unorm);
  CHECK(chosen.format == VK_FORMAT_B8G8R8A8_UNORM);
  CHECK(chosen.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
}

TEST_CASE("chooseSwapSurfaceFormat honours the sRGB preference") {
  std::vector<VkSurfaceFormatKHR> avail = {fmt(VK_FORMAT_B8G8R8A8_UNORM),
                                           fmt(VK_FORMAT_B8G8R8A8_SRGB)};
  auto chosen =
      vkutil::chooseSwapSurfaceFormat(avail, SurfaceFormatPreference::Srgb);
  CHECK(chosen.format == VK_FORMAT_B8G8R8A8_SRGB);
}

TEST_CASE("chooseSwapSurfaceFormat falls back to RGBA when BGRA is absent") {
  std::vector<VkSurfaceFormatKHR> avail = {fmt(VK_FORMAT_R8G8B8A8_UNORM)};
  auto chosen =
      vkutil::chooseSwapSurfaceFormat(avail, SurfaceFormatPreference::Unorm);
  CHECK(chosen.format == VK_FORMAT_R8G8B8A8_UNORM);
}

TEST_CASE("chooseSwapSurfaceFormat ignores matches in the wrong colour space") {
  // Right format, wrong colour space -> not a match; falls back to first.
  std::vector<VkSurfaceFormatKHR> avail = {
      fmt(VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT),
      fmt(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_HDR10_ST2084_EXT)};
  auto chosen =
      vkutil::chooseSwapSurfaceFormat(avail, SurfaceFormatPreference::Unorm);
  CHECK(chosen.format == avail[0].format);
  CHECK(chosen.colorSpace == avail[0].colorSpace);
}

// ---- chooseSwapPresentMode --------------------------------------------------

TEST_CASE("chooseSwapPresentMode returns FIFO for Vsync") {
  std::vector<VkPresentModeKHR> avail = {VK_PRESENT_MODE_FIFO_KHR,
                                         VK_PRESENT_MODE_MAILBOX_KHR};
  CHECK(vkutil::chooseSwapPresentMode(avail, PresentMode::Vsync) ==
        VK_PRESENT_MODE_FIFO_KHR);
}

TEST_CASE("chooseSwapPresentMode returns the requested mode when available") {
  std::vector<VkPresentModeKHR> avail = {VK_PRESENT_MODE_FIFO_KHR,
                                         VK_PRESENT_MODE_MAILBOX_KHR,
                                         VK_PRESENT_MODE_IMMEDIATE_KHR};
  CHECK(vkutil::chooseSwapPresentMode(avail, PresentMode::Mailbox) ==
        VK_PRESENT_MODE_MAILBOX_KHR);
  CHECK(vkutil::chooseSwapPresentMode(avail, PresentMode::Immediate) ==
        VK_PRESENT_MODE_IMMEDIATE_KHR);
}

TEST_CASE("chooseSwapPresentMode falls back to FIFO when preference absent") {
  std::vector<VkPresentModeKHR> avail = {VK_PRESENT_MODE_FIFO_KHR};
  CHECK(vkutil::chooseSwapPresentMode(avail, PresentMode::Mailbox) ==
        VK_PRESENT_MODE_FIFO_KHR);
  CHECK(vkutil::chooseSwapPresentMode(avail, PresentMode::Immediate) ==
        VK_PRESENT_MODE_FIFO_KHR);
}

// ---- clampSwapExtent --------------------------------------------------------

namespace {
auto caps(VkExtent2D current, VkExtent2D min, VkExtent2D max)
    -> VkSurfaceCapabilitiesKHR {
  VkSurfaceCapabilitiesKHR c{};
  c.currentExtent = current;
  c.minImageExtent = min;
  c.maxImageExtent = max;
  return c;
}
constexpr uint32_t kUndefined = std::numeric_limits<uint32_t>::max();
}  // namespace

TEST_CASE("clampSwapExtent returns currentExtent when the surface dictates it") {
  auto c = caps({800, 600}, {1, 1}, {4096, 4096});
  auto e = vkutil::clampSwapExtent(c, 1234, 5678);  // window size ignored
  CHECK(e.width == 800);
  CHECK(e.height == 600);
}

TEST_CASE("clampSwapExtent uses the window size within bounds") {
  auto c = caps({kUndefined, kUndefined}, {1, 1}, {4096, 4096});
  auto e = vkutil::clampSwapExtent(c, 1280, 720);
  CHECK(e.width == 1280);
  CHECK(e.height == 720);
}

TEST_CASE("clampSwapExtent clamps the window size to the allowed range") {
  auto c = caps({kUndefined, kUndefined}, {640, 480}, {1920, 1080});
  SUBCASE("below minimum") {
    auto e = vkutil::clampSwapExtent(c, 100, 50);
    CHECK(e.width == 640);
    CHECK(e.height == 480);
  }
  SUBCASE("above maximum") {
    auto e = vkutil::clampSwapExtent(c, 8000, 8000);
    CHECK(e.width == 1920);
    CHECK(e.height == 1080);
  }
}
