#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>

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

TEST_CASE("isSrgbFormat recognises every 8-bit sRGB format") {
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_R8G8B8A8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_B8G8R8A8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_R8G8B8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_B8G8R8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_A8B8G8R8_SRGB_PACK32));
  // The two single/dual-channel 8-bit sRGB formats are easy to miss but are
  // just as gamma-encoded as the RGB ones.
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_R8_SRGB));
  CHECK(vkutil::isSrgbFormat(VK_FORMAT_R8G8_SRGB));
}

TEST_CASE("isSrgbFormat rejects non-sRGB formats") {
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_R8G8B8A8_UNORM));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_B8G8R8A8_UNORM));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_R8_UNORM));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_UNDEFINED));
  CHECK_FALSE(vkutil::isSrgbFormat(VK_FORMAT_R16G16B16A16_SFLOAT));
}

// -----------------------------------------------------------------------------
// vkResultString
// -----------------------------------------------------------------------------
//
// The table below is NOT derived from the vkResultString switch: it is derived
// from the VkResult enum in the Vulkan headers, independent of the
// implementation, so it genuinely fails when the switch is missing a value that
// the build-time headers define.
//
// Extraction recipe (repeat when VK_HEADER_VERSION changes — see the
// static_assert below):
//   1. Parse the VkResult enum from vulkan_core.h.
//   2. Resolve alias enumerators to their numeric values.
//   3. Keep one "canonical" name per distinct value (the core names are the
//      1.4 spellings used by vkResultString on every build), plus any alias
//      spellings for that value that other header versions use, because a
//      switch case label may legally be an alias.
//   4. Group the values by the header version they first appear in so the test
//      compiles and passes on older headers: 38 values exist in 1.3.204, 9 more
//      arrived by 1.3.275 (the Ubuntu 24.04 libvulkan-dev headers the CI build
//      uses; all 47 are mapped unconditionally there), and 3 only exist in
//      1.4 headers newer than 1.3.275.
//
// The numeric values are written out (rather than the enum names) so this TU
// compiles against any Vulkan header version; the header-version gates decide
// which values are actually required to be mapped. The local 1.4.363 headers
// define 50 distinct values; 1.3.275 defines 47.

// The newest VK_HEADER_VERSION the table was extracted from. Bump the table
// (and this constant) when the project's Vulkan headers move past it; until
// then a newer header fails to compile with the message below instead of
// silently skipping the new codes.
static_assert(VK_HEADER_VERSION <= 363,
              "VkResult table in tests/test_utils.cpp was extracted from "
              "vulkan_core.h VK_HEADER_VERSION 363 — re-extract it (see the "
              "comment above) and bump this guard");

namespace {

struct ExpectedResult {
  int32_t value;
  /// Canonical symbolic name (the 1.4 header spelling).
  const char* name;
  /// Another spelling the same value has in some header versions; a switch
  /// case label may be written with either, so both are accepted here.
  const char* alias1{nullptr};
  const char* alias2{nullptr};
};

// clang-format off
// Values present in Vulkan headers 1.3.204 and older (38 distinct values).
constexpr ExpectedResult kBaselineResults[] = {
    {          0, "VK_SUCCESS"                                           },
    {          1, "VK_NOT_READY"                                         },
    {          2, "VK_TIMEOUT"                                           },
    {          3, "VK_EVENT_SET"                                         },
    {          4, "VK_EVENT_RESET"                                       },
    {          5, "VK_INCOMPLETE"                                        },
    {         -1, "VK_ERROR_OUT_OF_HOST_MEMORY"                          },
    {         -2, "VK_ERROR_OUT_OF_DEVICE_MEMORY"                        },
    {         -3, "VK_ERROR_INITIALIZATION_FAILED"                       },
    {         -4, "VK_ERROR_DEVICE_LOST"                                 },
    {         -5, "VK_ERROR_MEMORY_MAP_FAILED"                           },
    {         -6, "VK_ERROR_LAYER_NOT_PRESENT"                           },
    {         -7, "VK_ERROR_EXTENSION_NOT_PRESENT"                       },
    {         -8, "VK_ERROR_FEATURE_NOT_PRESENT"                         },
    {         -9, "VK_ERROR_INCOMPATIBLE_DRIVER"                         },
    {        -10, "VK_ERROR_TOO_MANY_OBJECTS"                            },
    {        -11, "VK_ERROR_FORMAT_NOT_SUPPORTED"                        },
    {        -12, "VK_ERROR_FRAGMENTED_POOL"                             },
    {        -13, "VK_ERROR_UNKNOWN"                                     },
    {-1000011001, "VK_ERROR_VALIDATION_FAILED"                           , "VK_ERROR_VALIDATION_FAILED_EXT"},
    {-1000069000, "VK_ERROR_OUT_OF_POOL_MEMORY"                          , "VK_ERROR_OUT_OF_POOL_MEMORY_KHR"},
    {-1000072003, "VK_ERROR_INVALID_EXTERNAL_HANDLE"                     , "VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR"},
    {-1000257000, "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS"              , "VK_ERROR_INVALID_DEVICE_ADDRESS_EXT", "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS_KHR"},
    {-1000161000, "VK_ERROR_FRAGMENTATION"                               , "VK_ERROR_FRAGMENTATION_EXT"},
    { 1000297000, "VK_PIPELINE_COMPILE_REQUIRED"                         , "VK_ERROR_PIPELINE_COMPILE_REQUIRED_EXT", "VK_PIPELINE_COMPILE_REQUIRED_EXT"},
    {-1000174001, "VK_ERROR_NOT_PERMITTED"                               , "VK_ERROR_NOT_PERMITTED_EXT", "VK_ERROR_NOT_PERMITTED_KHR"},
    {-1000000000, "VK_ERROR_SURFACE_LOST_KHR"                            },
    {-1000000001, "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR"                    },
    { 1000001003, "VK_SUBOPTIMAL_KHR"                                    },
    {-1000001004, "VK_ERROR_OUT_OF_DATE_KHR"                             },
    {-1000003001, "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR"                    },
    {-1000012000, "VK_ERROR_INVALID_SHADER_NV"                           },
    {-1000158000, "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT"},
    {-1000255000, "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT"         },
    { 1000268000, "VK_THREAD_IDLE_KHR"                                   },
    { 1000268001, "VK_THREAD_DONE_KHR"                                   },
    { 1000268002, "VK_OPERATION_DEFERRED_KHR"                            },
    { 1000268003, "VK_OPERATION_NOT_DEFERRED_KHR"                        },
};

// Values added after 1.3.204 and already present in 1.3.275 (9 values). The
// switch maps these unconditionally on a 1.3.275 build.
constexpr ExpectedResult kResultsAddedAfter204[] = {
    {-1000023000, "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR"            },
    {-1000023001, "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR"   },
    {-1000023002, "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR"},
    {-1000023003, "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR"   },
    {-1000023004, "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR"    },
    {-1000023005, "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR"      },
    {-1000299000, "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR"         },
    {-1000338000, "VK_ERROR_COMPRESSION_EXHAUSTED_EXT"                },
    { 1000482000, "VK_INCOMPATIBLE_SHADER_BINARY_EXT"                 , "VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT"},
};

// Values that only exist in 1.4-era headers after 1.3.275 (3 values). The
// switch maps them behind `#if VK_HEADER_VERSION >= 294` / `>= 335`; this test
// requires them only on 1.4 headers newer than that, and is conservative
// (VK_HEADER_VERSION >= 363) so it can never demand a mapping the build could
// not have seen.
constexpr ExpectedResult kResultsAddedAfter275[] = {
    {-1000208000, "VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT"},
    { 1000483000, "VK_PIPELINE_BINARY_MISSING_KHR"        },
    {-1000483000, "VK_ERROR_NOT_ENOUGH_SPACE_KHR"         },
};
// clang-format on

void checkResultsAreMapped(const ExpectedResult* entries, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const ExpectedResult& entry = entries[i];
    INFO("VkResult value ", entry.value, " (", entry.name, ")");
    const std::string actual =
        vkutil::vkResultString(static_cast<VkResult>(entry.value));
    CHECK_MESSAGE(actual != "VK_RESULT_<unknown>", "value ", entry.value,
                  " maps to the unknown fallback");
    const bool nameMatches =
        actual == entry.name ||
        (entry.alias1 != nullptr && actual == entry.alias1) ||
        (entry.alias2 != nullptr && actual == entry.alias2);
    CHECK_MESSAGE(nameMatches, "value ", entry.value, " is named '", actual,
                  "', expected one of the header spellings");
  }
}

}  // namespace

TEST_CASE("vkResultString names every result value in the Vulkan headers") {
  checkResultsAreMapped(kBaselineResults, std::size(kBaselineResults));
#if VK_HEADER_VERSION >= 275
  // Ubuntu 24.04's libvulkan-dev (1.3.275) is the oldest header set the CI
  // build uses; every value defined there must map to a symbolic name.
  checkResultsAreMapped(kResultsAddedAfter204,
                        std::size(kResultsAddedAfter204));
#endif
#if VK_HEADER_VERSION >= 363
  checkResultsAreMapped(kResultsAddedAfter275,
                        std::size(kResultsAddedAfter275));
#endif
}

TEST_CASE("vkResultString falls back for unknown codes") {
  CHECK(std::string(vkutil::vkResultString(
            static_cast<VkResult>(0x7FFFFFFF))) == "VK_RESULT_<unknown>");
}

// -----------------------------------------------------------------------------
// createShaderModule input validation
// -----------------------------------------------------------------------------
//
// These cases exercise the documented argument validation, which runs before
// any Vulkan call: that is why they can use VK_NULL_HANDLE as the device and
// still run on machines without an ICD (the success path lives in
// test_utils_gpu.cpp, where a real device exists).

TEST_CASE("createShaderModule rejects a null SPIR-V pointer") {
  CHECK_THROWS_AS((void)vkutil::createShaderModule(VK_NULL_HANDLE, nullptr, 4),
                  std::invalid_argument);
}

TEST_CASE("createShaderModule rejects empty or unaligned SPIR-V byte sizes") {
  constexpr uint32_t kSpirvHeader[] = {0x07230203, 0x00010300, 0x00000000,
                                       0x00000001, 0x00000000};
  CHECK_THROWS_AS(
      (void)vkutil::createShaderModule(VK_NULL_HANDLE, kSpirvHeader, 0),
      std::invalid_argument);
  // SPIR-V is stored as 32-bit words; a byte count that is not a multiple of
  // four cannot be a whole module.
  CHECK_THROWS_AS(
      (void)vkutil::createShaderModule(VK_NULL_HANDLE, kSpirvHeader, 18),
      std::invalid_argument);
  CHECK_THROWS_AS(
      (void)vkutil::createShaderModule(VK_NULL_HANDLE, kSpirvHeader, 21),
      std::invalid_argument);
}

// -----------------------------------------------------------------------------
// vkCheck
// -----------------------------------------------------------------------------

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

TEST_CASE("vkCheck formats an unrecognised result with the unknown fallback") {
  // The message must still carry the context even when vkResultString has no
  // symbolic name for the code.
  try {
    vkutil::vkCheck(static_cast<VkResult>(0x7FFFFFFF), "creating widget");
    FAIL("expected vkCheck to throw");
  } catch (const std::runtime_error& e) {
    const std::string msg = e.what();
    CHECK(msg.find("creating widget") != std::string::npos);
    CHECK(msg.find("VK_RESULT_<unknown>") != std::string::npos);
  }
}
