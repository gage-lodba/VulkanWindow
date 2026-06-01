#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

// Forward declarations for VMA opaque handles so consumers can use these
// helpers without pulling in vk_mem_alloc.h.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T *;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T *;

namespace vkutil {

/// A buffer paired with its VMA allocation. `mapped` is non-null only for
/// host-visible, persistently-mapped buffers (see createMappedBuffer); write
/// CPU-side data through it.
struct Buffer {
  VkBuffer buffer{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  VkDeviceSize size{0};
  void *mapped{nullptr};
};

/// Create a device-local (GPU) buffer and upload `size` bytes from `data` into
/// it via a temporary staging buffer, copied on `queue`. A transient command
/// pool is created and destroyed internally. `usage` is the final buffer's
/// usage; VK_BUFFER_USAGE_TRANSFER_DST_BIT is added for you. Blocks until the
/// copy completes (vkQueueWaitIdle), so it's for one-shot / load-time uploads,
/// not the per-frame hot path. Throws std::runtime_error on failure.
[[nodiscard]] auto createDeviceLocalBuffer(VmaAllocator allocator,
                                           VkDevice device, VkQueue queue,
                                           uint32_t queueFamily,
                                           const void *data, VkDeviceSize size,
                                           VkBufferUsageFlags usage) -> Buffer;

/// Create a host-visible, persistently-mapped buffer for data you write from
/// the CPU (uniforms, dynamic vertices). Write through `Buffer::mapped`; the
/// writes are made visible to the GPU on the next queue submit. Intended for
/// sequential writes (e.g. memcpy) — don't read it back. Throws on failure.
[[nodiscard]] auto createMappedBuffer(VmaAllocator allocator, VkDeviceSize size,
                                      VkBufferUsageFlags usage) -> Buffer;

/// Destroy a buffer created by the helpers above and null its handles. A no-op
/// on a default-constructed / already-destroyed Buffer.
void destroyBuffer(VmaAllocator allocator, Buffer &buffer);

/// Allocate and begin a primary command buffer for a one-shot submission
/// (ONE_TIME_SUBMIT). Pair with endSingleTimeCommands. Throws on failure.
[[nodiscard]] auto beginSingleTimeCommands(VkDevice device,
                                           VkCommandPool commandPool)
    -> VkCommandBuffer;

/// End, submit, and wait for a command buffer from beginSingleTimeCommands,
/// then free it. Blocks on vkQueueWaitIdle. Throws on failure.
void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool,
                           VkQueue queue, VkCommandBuffer commandBuffer);

/// A 2D image paired with its VMA allocation and a view covering all mips.
struct Image {
  VkImage image{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};
  VkExtent2D extent{};
  VkFormat format{VK_FORMAT_UNDEFINED};
  uint32_t mipLevels{1};
};

/// Create a 2D sampled texture from tightly-packed pixel data: a device-local
/// image, staged upload, transitioned to SHADER_READ_ONLY_OPTIMAL, plus a view.
/// `data`/`dataSize` is level 0 (`width`×`height` texels of `format`). When
/// `generateMipmaps` is true a full mip chain is built by linear blits — which
/// requires `format` to support linear-filter blitting on optimal tiling (it
/// throws otherwise). A transient command pool is created/destroyed internally
/// and the upload blocks (vkQueueWaitIdle), so this is for load-time use.
/// Throws std::runtime_error on failure.
[[nodiscard]] auto createTexture2D(VmaAllocator allocator,
                                   VkPhysicalDevice physicalDevice,
                                   VkDevice device, VkQueue queue,
                                   uint32_t queueFamily, const void *data,
                                   VkDeviceSize dataSize, uint32_t width,
                                   uint32_t height,
                                   VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                                   bool generateMipmaps = false) -> Image;

/// Record an image layout transition into `commandBuffer`. Access masks and
/// pipeline stages are chosen for the common texture-upload transitions
/// (UNDEFINED→TRANSFER_DST, TRANSFER_DST→SHADER_READ_ONLY,
/// UNDEFINED→SHADER_READ_ONLY); other layout pairs fall back to a conservative
/// ALL_COMMANDS / MEMORY_READ|WRITE barrier. Covers mip levels [0, mipLevels).
void transitionImageLayout(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
    VkImageLayout newLayout, uint32_t mipLevels = 1,
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

/// Create a sampler. Mip sampling is on (linear, maxLod unclamped). Anisotropy
/// is off — that device feature isn't enabled by the scaffold. Throws on
/// failure; destroy with vkDestroySampler.
[[nodiscard]] auto createSampler(
    VkDevice device, VkFilter filter = VK_FILTER_LINEAR,
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT)
    -> VkSampler;

/// Destroy an Image from createTexture2D (view + image + allocation) and null
/// its handles. A no-op on a default-constructed / already-destroyed Image.
void destroyImage(VmaAllocator allocator, VkDevice device, Image &image);

}  // namespace vkutil
