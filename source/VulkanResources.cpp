#include "VulkanResources.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "VulkanUtils.h"  // vkCheck

namespace vkutil {

namespace {

auto makeBuffer(VmaAllocator allocator, VkDeviceSize size,
                VkBufferUsageFlags usage,
                VmaAllocationCreateFlags allocFlags) -> Buffer {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocCreateInfo{};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocCreateInfo.flags = allocFlags;

  Buffer result{};
  result.size = size;
  VmaAllocationInfo allocInfo{};
  vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo,
                          &result.buffer, &result.allocation, &allocInfo),
          "Failed to create buffer");
  result.mapped = allocInfo.pMappedData;  // non-null only with MAPPED_BIT
  return result;
}

}  // namespace

auto createMappedBuffer(VmaAllocator allocator, VkDeviceSize size,
                        VkBufferUsageFlags usage) -> Buffer {
  return makeBuffer(allocator, size, usage,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                        VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void destroyBuffer(VmaAllocator allocator, Buffer &buffer) {
  if (buffer.buffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
  }
  buffer.buffer = VK_NULL_HANDLE;
  buffer.allocation = VK_NULL_HANDLE;
  buffer.size = 0;
  buffer.mapped = nullptr;
}

auto beginSingleTimeCommands(VkDevice device,
                             VkCommandPool commandPool) -> VkCommandBuffer {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer),
          "Failed to allocate single-time command buffer");

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo),
          "Failed to begin single-time command buffer");
  return commandBuffer;
}

void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool,
                           VkQueue queue, VkCommandBuffer commandBuffer) {
  vkCheck(vkEndCommandBuffer(commandBuffer),
          "Failed to end single-time command buffer");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  vkCheck(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE),
          "Failed to submit single-time command buffer");
  vkCheck(vkQueueWaitIdle(queue), "Failed waiting on single-time commands");

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

auto createDeviceLocalBuffer(VmaAllocator allocator, VkDevice device,
                             VkQueue queue, uint32_t queueFamily,
                             const void *data, VkDeviceSize size,
                             VkBufferUsageFlags usage) -> Buffer {
  Buffer staging =
      createMappedBuffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  // createMappedBuffer always persistently maps, but guard so the memcpy below
  // can't run on a null pointer if that ever changes.
  if (staging.mapped == nullptr) {
    destroyBuffer(allocator, staging);
    throw std::runtime_error("Staging buffer was not host-mapped");
  }
  std::memcpy(staging.mapped, data, static_cast<size_t>(size));
  // No-op for coherent memory; required before the device reads non-coherent
  // memory. The subsequent queue submit makes the flushed host writes visible.
  vmaFlushAllocation(allocator, staging.allocation, 0, VK_WHOLE_SIZE);

  Buffer result =
      makeBuffer(allocator, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = queueFamily;

  VkCommandPool pool = VK_NULL_HANDLE;
  try {
    vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool),
            "Failed to create transfer command pool");

    VkCommandBuffer cmd = beginSingleTimeCommands(device, pool);
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.buffer, result.buffer, 1, &copyRegion);

    // Make the transfer write available/visible to any later read (vertex,
    // index, uniform, ...) so a consumer that binds this buffer in a later
    // submit doesn't need its own barrier. Broad dst scope is fine for a
    // one-shot upload.
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);

    endSingleTimeCommands(device, pool, queue, cmd);
  } catch (...) {
    if (pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, pool, nullptr);
    }
    destroyBuffer(allocator, result);
    destroyBuffer(allocator, staging);
    throw;
  }

  vkDestroyCommandPool(device, pool, nullptr);
  destroyBuffer(allocator, staging);
  return result;
}

void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t mipLevels, VkImageAspectFlags aspectMask) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspectMask;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage = 0;
  VkPipelineStageFlags dstStage = 0;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    // Conservative fallback for any other pair.
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

auto createSampler(VkDevice device, VkFilter filter,
                   VkSamplerAddressMode addressMode) -> VkSampler {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = filter;
  samplerInfo.minFilter = filter;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = addressMode;
  samplerInfo.addressModeV = addressMode;
  samplerInfo.addressModeW = addressMode;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

  VkSampler sampler = VK_NULL_HANDLE;
  vkCheck(vkCreateSampler(device, &samplerInfo, nullptr, &sampler),
          "Failed to create sampler");
  return sampler;
}

void destroyImage(VmaAllocator allocator, VkDevice device, Image &image) {
  if (image.view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, image.view, nullptr);
  }
  if (image.image != VK_NULL_HANDLE) {
    vmaDestroyImage(allocator, image.image, image.allocation);
  }
  image = Image{};
}

namespace {

auto computeMipLevels(uint32_t width, uint32_t height) -> uint32_t {
  uint32_t levels = 1;
  uint32_t dim = std::max(width, height);
  while (dim > 1) {
    dim >>= 1U;
    ++levels;
  }
  return levels;
}

auto makeImage(VmaAllocator allocator, uint32_t width, uint32_t height,
               uint32_t mipLevels, VkFormat format,
               VkImageUsageFlags usage) -> Image {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {.width = width, .height = height, .depth = 1};
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocCreateInfo{};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

  Image result{};
  result.extent = {.width = width, .height = height};
  result.format = format;
  result.mipLevels = mipLevels;
  vkCheck(vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &result.image,
                         &result.allocation, nullptr),
          "Failed to create image");
  return result;
}

// Record a full mip chain by blitting each level down from the previous one,
// leaving every level in SHADER_READ_ONLY_OPTIMAL. Level 0 must already hold
// the source data in TRANSFER_DST_OPTIMAL (as do all other levels).
void recordMipChain(VkCommandBuffer cmd, VkImage image, uint32_t width,
                    uint32_t height, uint32_t mipLevels) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  auto mipWidth = static_cast<int32_t>(width);
  auto mipHeight = static_cast<int32_t>(height);

  for (uint32_t i = 1; i < mipLevels; i++) {
    // Source level (i-1): TRANSFER_DST -> TRANSFER_SRC for the blit read.
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkImageBlit blit{};
    blit.srcOffsets[1] = {.x = mipWidth, .y = mipHeight, .z = 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[1] = {.x = mipWidth > 1 ? mipWidth / 2 : 1,
                          .y = mipHeight > 1 ? mipHeight / 2 : 1,
                          .z = 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    // Source level is done — to SHADER_READ_ONLY for sampling.
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);

    if (mipWidth > 1) mipWidth /= 2;
    if (mipHeight > 1) mipHeight /= 2;
  }

  // The last level was only ever a blit destination — TRANSFER_DST -> READ.
  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

}  // namespace

auto createTexture2D(VmaAllocator allocator, VkPhysicalDevice physicalDevice,
                     VkDevice device, VkQueue queue, uint32_t queueFamily,
                     const void *data, VkDeviceSize dataSize, uint32_t width,
                     uint32_t height, VkFormat format,
                     bool generateMipmaps) -> Image {
  const uint32_t mipLevels =
      generateMipmaps ? computeMipLevels(width, height) : 1U;

  if (mipLevels > 1) {
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
    if ((props.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0) {
      throw std::runtime_error(
          "Texture format does not support linear blit for mipmap generation");
    }
  }

  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (mipLevels > 1) {
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }

  Image result = makeImage(allocator, width, height, mipLevels, format, usage);

  Buffer staging =
      createMappedBuffer(allocator, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  if (staging.mapped == nullptr) {
    destroyBuffer(allocator, staging);
    destroyImage(allocator, device, result);
    throw std::runtime_error("Staging buffer was not host-mapped");
  }
  std::memcpy(staging.mapped, data, static_cast<size_t>(dataSize));
  vmaFlushAllocation(allocator, staging.allocation, 0, VK_WHOLE_SIZE);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = queueFamily;

  VkCommandPool pool = VK_NULL_HANDLE;
  try {
    vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool),
            "Failed to create transfer command pool");

    VkCommandBuffer cmd = beginSingleTimeCommands(device, pool);

    transitionImageLayout(cmd, result.image, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {.width = width, .height = height, .depth = 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, result.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (mipLevels > 1) {
      recordMipChain(cmd, result.image, width, height, mipLevels);
    } else {
      transitionImageLayout(
          cmd, result.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
    }

    endSingleTimeCommands(device, pool, queue, cmd);
  } catch (...) {
    if (pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, pool, nullptr);
    }
    destroyBuffer(allocator, staging);
    destroyImage(allocator, device, result);
    throw;
  }

  vkDestroyCommandPool(device, pool, nullptr);
  destroyBuffer(allocator, staging);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = result.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device, &viewInfo, nullptr, &result.view) !=
      VK_SUCCESS) {
    destroyImage(allocator, device, result);
    throw std::runtime_error("Failed to create texture image view");
  }

  return result;
}

}  // namespace vkutil
