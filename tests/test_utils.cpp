#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>

#include "VulkanUtils.h"

TEST_CASE("srgbToLinear maps the endpoints to themselves") {
  CHECK(vkutil::srgbToLinear(0.0f) == doctest::Approx(0.0f));
  CHECK(vkutil::srgbToLinear(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("srgbToLinear uses the linear segment below the 0.04045 threshold") {
  // Below the cutoff the transfer function is a straight line / 12.92.
  CHECK(vkutil::srgbToLinear(0.04045f) == doctest::Approx(0.04045f / 12.92f));
  CHECK(vkutil::srgbToLinear(0.02f) == doctest::Approx(0.02f / 12.92f));
}

TEST_CASE("srgbToLinear uses the power segment above the threshold") {
  // A mid-grey 0.5 sRGB is ~0.214 linear (the canonical reference value).
  CHECK(vkutil::srgbToLinear(0.5f) ==
        doctest::Approx(0.21404114f).epsilon(1e-5));
  const float expected = std::pow((0.8f + 0.055f) / 1.055f, 2.4f);
  CHECK(vkutil::srgbToLinear(0.8f) == doctest::Approx(expected));
}

TEST_CASE("srgbToLinear is monotonically increasing and darkens midtones") {
  CHECK(vkutil::srgbToLinear(0.25f) < vkutil::srgbToLinear(0.5f));
  CHECK(vkutil::srgbToLinear(0.5f) < vkutil::srgbToLinear(0.75f));
  // The sRGB curve sits below the identity line in the midtones.
  CHECK(vkutil::srgbToLinear(0.5f) < 0.5f);
}

TEST_CASE("isSrgbFormat recognises the 8-bit sRGB surface formats") {
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_R8G8B8A8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_B8G8R8A8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_R8G8B8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_B8G8R8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_A8B8G8R8_SRGB_PACK32));
}

TEST_CASE("isSrgbFormat rejects non-sRGB formats") {
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_R8G8B8A8_UNORM));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_B8G8R8A8_UNORM));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_UNDEFINED));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_R16G16B16A16_SFLOAT));
}

TEST_CASE("vkResultString returns the symbolic name for every known code") {
  // Every VkResult code in the vkResultString switch — ensure none return the
  // unknown fallback.  Enumerating them explicitly catches missing branches
  // when new codes are added to the Vulkan headers.
  // Use a struct so the range-for doesn't need C++20 structured bindings.
  struct Entry {
    VkResult code;
    const char* name;
  };
  const Entry kCodes[] = {
      {VK_SUCCESS, "VK_SUCCESS"},
      {VK_NOT_READY, "VK_NOT_READY"},
      {VK_TIMEOUT, "VK_TIMEOUT"},
      {VK_EVENT_SET, "VK_EVENT_SET"},
      {VK_EVENT_RESET, "VK_EVENT_RESET"},
      {VK_INCOMPLETE, "VK_INCOMPLETE"},
      {VK_ERROR_OUT_OF_HOST_MEMORY, "VK_ERROR_OUT_OF_HOST_MEMORY"},
      {VK_ERROR_OUT_OF_DEVICE_MEMORY, "VK_ERROR_OUT_OF_DEVICE_MEMORY"},
      {VK_ERROR_INITIALIZATION_FAILED, "VK_ERROR_INITIALIZATION_FAILED"},
      {VK_ERROR_DEVICE_LOST, "VK_ERROR_DEVICE_LOST"},
      {VK_ERROR_MEMORY_MAP_FAILED, "VK_ERROR_MEMORY_MAP_FAILED"},
      {VK_ERROR_LAYER_NOT_PRESENT, "VK_ERROR_LAYER_NOT_PRESENT"},
      {VK_ERROR_EXTENSION_NOT_PRESENT, "VK_ERROR_EXTENSION_NOT_PRESENT"},
      {VK_ERROR_FEATURE_NOT_PRESENT, "VK_ERROR_FEATURE_NOT_PRESENT"},
      {VK_ERROR_INCOMPATIBLE_DRIVER, "VK_ERROR_INCOMPATIBLE_DRIVER"},
      {VK_ERROR_TOO_MANY_OBJECTS, "VK_ERROR_TOO_MANY_OBJECTS"},
      {VK_ERROR_FORMAT_NOT_SUPPORTED, "VK_ERROR_FORMAT_NOT_SUPPORTED"},
      {VK_ERROR_FRAGMENTED_POOL, "VK_ERROR_FRAGMENTED_POOL"},
      {VK_ERROR_UNKNOWN, "VK_ERROR_UNKNOWN"},
      {VK_ERROR_OUT_OF_POOL_MEMORY, "VK_ERROR_OUT_OF_POOL_MEMORY"},
      {VK_ERROR_INVALID_EXTERNAL_HANDLE, "VK_ERROR_INVALID_EXTERNAL_HANDLE"},
      {VK_ERROR_FRAGMENTATION, "VK_ERROR_FRAGMENTATION"},
      {VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS,
       "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS"},
      {VK_ERROR_SURFACE_LOST_KHR, "VK_ERROR_SURFACE_LOST_KHR"},
      {VK_ERROR_NATIVE_WINDOW_IN_USE_KHR, "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR"},
      {VK_SUBOPTIMAL_KHR, "VK_SUBOPTIMAL_KHR"},
      {VK_ERROR_OUT_OF_DATE_KHR, "VK_ERROR_OUT_OF_DATE_KHR"},
      {VK_ERROR_INCOMPATIBLE_DISPLAY_KHR, "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR"},
      {VK_ERROR_VALIDATION_FAILED_EXT, "VK_ERROR_VALIDATION_FAILED_EXT"},
  };
  for (const auto& entry : kCodes) {
    INFO("code=", static_cast<int>(entry.code));
    CHECK(std::string(vkutil::vkResultString(entry.code)) == entry.name);
  }
}

TEST_CASE("vkResultString falls back for unknown codes") {
  CHECK(std::string(vkutil::vkResultString(
            static_cast<VkResult>(0x7FFFFFFF))) == "VK_RESULT_<unknown>");
}

TEST_CASE("vkCheck is a no-op on VK_SUCCESS") {
  CHECK_NOTHROW(vkutil::vkCheck(VK_SUCCESS, "should not throw"));
}

TEST_CASE("vkCheck throws with the context and result name on failure") {
  try {
    vkutil::vkCheck(VK_ERROR_DEVICE_LOST, "creating widget");
    FAIL("expected vkCheck to throw");
  } catch (const std::runtime_error& e) {
    const std::string msg = e.what();
    CHECK(msg.find("creating widget") != std::string::npos);
    CHECK(msg.find("VK_ERROR_DEVICE_LOST") != std::string::npos);
  }
}
