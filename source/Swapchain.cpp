#include "Swapchain.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "VulkanContext.h"
#include "VulkanRenderer.h"  // PresentMode
#include "VulkanSelectors.h"
#include "VulkanUtils.h"

using vkutil::querySwapChainSupport;
using vkutil::SwapChainSupportDetails;
using vkutil::vkCheck;

namespace {

// Thin GLFW-aware wrapper over vkutil::clampSwapExtent: fetch the current
// framebuffer size, then delegate the pure clamp/passthrough logic (which is
// unit-tested in isolation).
auto chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                      GLFWwindow *window) -> VkExtent2D {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  VkExtent2D extent = vkutil::clampSwapExtent(
      capabilities, width > 0 ? static_cast<uint32_t>(width) : 0u,
      height > 0 ? static_cast<uint32_t>(height) : 0u);
  // A minimized window reports a 0×0 framebuffer, and on Win32 the surface
  // then reports currentExtent = minImageExtent = maxImageExtent = 0×0 as well
  // (measured on AMD 1.4.349), so clampSwapExtent hands back a zero extent.
  // A zero/short extent is only repairable when the surface still exposes
  // usable bounds: prefer minImageExtent, and use 1×1 when the minimum itself
  // is zero. If maxImageExtent is zero the bounds are degenerate and no legal
  // extent exists (vkCreateSwapchainKHR would violate
  // VUID-VkSwapchainCreateInfoKHR-imageExtent-01689 for a zero extent, and
  // VUID-VkSwapchainCreateInfoKHR-pNext-07781 for anything outside
  // [minImageExtent, maxImageExtent]) — leave it zero so createSwapchain()
  // reports that instead of handing the driver an invalid extent.
  if (extent.width == 0) {
    extent.width = capabilities.maxImageExtent.width == 0
                       ? 0u
                       : std::max(capabilities.minImageExtent.width, 1u);
  }
  if (extent.height == 0) {
    extent.height = capabilities.maxImageExtent.height == 0
                        ? 0u
                        : std::max(capabilities.minImageExtent.height, 1u);
  }
  return extent;
}

}  // namespace

Swapchain::Swapchain(VulkanContext &ctx, GLFWwindow *window,
                     PresentMode preferred,
                     SurfaceFormatPreference formatPreference)
    : ctx(ctx), window(window), formatPreference(formatPreference) {
  try {
    createSwapchain(preferred);
    createImageViews();
    createDepthResources();
    createRenderPass();
    createFramebuffers();
  } catch (...) {
    cleanup();
    throw;
  }
}

Swapchain::~Swapchain() { cleanup(); }

auto Swapchain::recreate(PresentMode preferred) -> RecreateResult {
  // Caller is responsible for vkDeviceWaitIdle before this call.
  VkFormat oldFormat = imageFormat;
  size_t oldImageCount = images.size();

  // Tear down dependent state first; the swap-chain itself is recreated via
  // oldSwapchain inside createSwapchain.
  destroyImageResources();

  createSwapchain(preferred);

  const bool formatChanged = imageFormat != oldFormat;
  const bool imageCountChanged = images.size() != oldImageCount;

  // The render pass references the surface format; if that changed, the pass
  // is incompatible and must be rebuilt. Hand the old pass back to the caller
  // for destruction so anything that pinned it (e.g. ImGui's internal
  // pipelines) can be torn down first.
  VkRenderPass oldRenderPass = VK_NULL_HANDLE;
  if (formatChanged) {
    oldRenderPass = renderPass;
    renderPass = VK_NULL_HANDLE;
  }

  // If any step throws after the old render pass has been extracted, the local
  // handle would leak — the member is already null.  Wrap the rest so a throw
  // destroys the local and propagates.
  try {
    createImageViews();
    createDepthResources();

    if (formatChanged) {
      createRenderPass();
    }

    createFramebuffers();
  } catch (...) {
    if (oldRenderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(ctx.device, oldRenderPass, nullptr);
    }
    throw;
  }

  return {.formatChanged = formatChanged,
          .imageCountChanged = imageCountChanged,
          .oldRenderPassToDestroy = oldRenderPass};
}

void Swapchain::createSwapchain(PresentMode preferred) {
  SwapChainSupportDetails support =
      querySwapChainSupport(ctx.physicalDevice, ctx.surface);

  VkSurfaceFormatKHR surfaceFormat =
      vkutil::chooseSwapSurfaceFormat(support.formats, formatPreference);
  VkPresentModeKHR presentMode =
      vkutil::chooseSwapPresentMode(support.presentModes, preferred);
  VkExtent2D chosenExtent = chooseSwapExtent(support.capabilities, window);
  if (chosenExtent.width == 0 || chosenExtent.height == 0) {
    // No legal extent exists while the window has no drawable area (Win32
    // reports degenerate surface bounds for a minimized window). Failing with
    // an actionable message beats handing vkCreateSwapchainKHR an extent that
    // violates the spec — some drivers accept it silently. This affects the
    // *initial* creation only: recreateSwapChain() already defers while the
    // framebuffer is 0×0, so minimizing at runtime is handled.
    throw std::runtime_error(
        "Cannot create a swap chain: the window has no drawable area "
        "(framebuffer is 0×0 and the surface reports degenerate bounds). "
        "Restore/un-minimize the window before creating the renderer.");
  }

  // chooseSwapPresentMode is pure (no logging); report a fallback here. Warn
  // once per process — createSwapchain runs on every resize, so warning each
  // time would be noise.
  if (preferred != PresentMode::Vsync &&
      presentMode == VK_PRESENT_MODE_FIFO_KHR) {
    static bool warned = false;
    if (!warned) {
      std::cerr << "Warning: preferred present mode "
                << (preferred == PresentMode::Mailbox ? "Mailbox" : "Immediate")
                << " unavailable; falling back to VK_PRESENT_MODE_FIFO_KHR.\n";
      warned = true;
    }
  }

  // Mailbox benefits from triple buffering (one acquired, one queued, one
  // displayed); request an extra image so the driver doesn't have to discard
  // newly-rendered frames as aggressively. FIFO/Immediate are fine with +1.
  const uint32_t requestedExtra =
      presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? 2 : 1;
  uint32_t imageCount = support.capabilities.minImageCount + requestedExtra;
  if (support.capabilities.maxImageCount > 0 &&
      imageCount > support.capabilities.maxImageCount) {
    imageCount = support.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = ctx.surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = chosenExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  std::array queueFamilyIndices = {ctx.graphicsQueueFamily,
                                   ctx.presentQueueFamily};

  if (ctx.graphicsQueueFamily != ctx.presentQueueFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = support.capabilities.currentTransform;

  // Prefer opaque (the GLFW window is non-transparent) and fall back to the
  // first flag the surface advertises — hardcoding OPAQUE fails on surfaces
  // that don't support it.
  if (support.capabilities.supportedCompositeAlpha &
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  } else if (support.capabilities.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  } else {
    // Last resort: pick a single supported bit. compositeAlpha is a
    // *FlagBits value — exactly one bit — so passing the raw multi-bit mask
    // through would be an invalid enum value.
    const VkCompositeAlphaFlagsKHR supported =
        support.capabilities.supportedCompositeAlpha;
    createInfo.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
        supported & (~supported + 1u));  // lowest set bit
  }

  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  minImageCount = imageCount;

  VkSwapchainKHR oldSwapchain = swapChain;
  createInfo.oldSwapchain = oldSwapchain;

  // Create into a local: per spec the contents of an output parameter are
  // undefined when a command fails, so writing straight into `swapChain`
  // could leave the member holding a garbage handle — and lose the still-valid
  // old swap chain, which cleanup() would then leak. The old handle is only
  // retired, and destroyed, after a successful create.
  VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(ctx.device, &createInfo, nullptr, &newSwapchain) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create swap chain");
  }
  swapChain = newSwapchain;

  if (oldSwapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(ctx.device, oldSwapchain, nullptr);
  }

  // A failed (or partial) query must not leave stale handles in `images` for
  // createImageViews() to feed to vkCreateImageView. VK_INCOMPLETE — the
  // count grew between the two calls — is surfaced as an error too, since a
  // truncated image list is not something we can render with.
  vkCheck(vkGetSwapchainImagesKHR(ctx.device, swapChain, &imageCount, nullptr),
          "Failed to query swap chain image count");
  images.resize(imageCount);
  vkCheck(vkGetSwapchainImagesKHR(ctx.device, swapChain, &imageCount,
                                  images.data()),
          "Failed to get swap chain images");

  imageFormat = surfaceFormat.format;
  extent = chosenExtent;
}

void Swapchain::createImageViews() {
  imageViews.resize(images.size());

  for (size_t i = 0; i < images.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = images[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = imageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    vkCheck(vkCreateImageView(ctx.device, &createInfo, nullptr, &imageViews[i]),
            "Failed to create swap-chain image view");
  }
}

void Swapchain::createRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = imageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = depthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  // External → 0. The swap-chain (color) image is made available by the
  // imageAvailableSemaphore wait at COLOR_ATTACHMENT_OUTPUT, so it needs no
  // color srcAccessMask. The depth image, however, is reused every frame: the
  // previous frame's depth writes (LATE_FRAGMENT_TESTS) must be made available
  // before this frame's begin-render-pass layout transition, or synchronization
  // validation reports a WRITE_AFTER_WRITE hazard (transition vs. prior store).
  // Hence srcAccessMask carries the depth-attachment write.
  VkSubpassDependency depIn{};
  depIn.srcSubpass = VK_SUBPASS_EXTERNAL;
  depIn.dstSubpass = 0;
  depIn.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  depIn.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  depIn.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  depIn.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  depIn.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  // 0 → External: explicit transition to PRESENT_SRC_KHR. Without this
  // Vulkan inserts a conservative implicit dependency; making it explicit
  // and minimal is cheaper.
  VkSubpassDependency depOut{};
  depOut.srcSubpass = 0;
  depOut.dstSubpass = VK_SUBPASS_EXTERNAL;
  depOut.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  depOut.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  depOut.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  depOut.dstAccessMask = 0;
  depOut.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  std::array attachments = {colorAttachment, depthAttachment};
  std::array dependencies = {depIn, depOut};

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  vkCheck(vkCreateRenderPass(ctx.device, &renderPassInfo, nullptr, &renderPass),
          "Failed to create render pass");
}

void Swapchain::createFramebuffers() {
  if (ctx.useImagelessFramebuffer) {
    // Imageless framebuffer (Vulkan 1.2): one framebuffer for all swapchain
    // images; actual image views are provided at vkCmdBeginRenderPass time.
    VkFramebufferAttachmentImageInfo colorAttachmentImageInfo{};
    colorAttachmentImageInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
    colorAttachmentImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    colorAttachmentImageInfo.width = extent.width;
    colorAttachmentImageInfo.height = extent.height;
    colorAttachmentImageInfo.layerCount = 1;
    colorAttachmentImageInfo.viewFormatCount = 1;
    colorAttachmentImageInfo.pViewFormats = &imageFormat;

    VkFramebufferAttachmentImageInfo depthAttachmentImageInfo{};
    depthAttachmentImageInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
    depthAttachmentImageInfo.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthAttachmentImageInfo.width = extent.width;
    depthAttachmentImageInfo.height = extent.height;
    depthAttachmentImageInfo.layerCount = 1;
    depthAttachmentImageInfo.viewFormatCount = 1;
    depthAttachmentImageInfo.pViewFormats = &depthFormat;

    std::array attachmentImageInfos = {colorAttachmentImageInfo,
                                       depthAttachmentImageInfo};

    VkFramebufferAttachmentsCreateInfo attachmentsCreateInfo{};
    attachmentsCreateInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
    attachmentsCreateInfo.attachmentImageInfoCount =
        static_cast<uint32_t>(attachmentImageInfos.size());
    attachmentsCreateInfo.pAttachmentImageInfos = attachmentImageInfos.data();

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
    framebufferInfo.pNext = &attachmentsCreateInfo;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount =
        static_cast<uint32_t>(attachmentImageInfos.size());
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    framebuffers.resize(1);
    vkCheck(vkCreateFramebuffer(ctx.device, &framebufferInfo, nullptr,
                                &framebuffers[0]),
            "Failed to create imageless framebuffer");
  } else {
    // Traditional: one framebuffer per swapchain image.
    framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
      std::array attachments = {imageViews[i], depthImageViews[i]};

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount =
          static_cast<uint32_t>(attachments.size());
      framebufferInfo.pAttachments = attachments.data();
      framebufferInfo.width = extent.width;
      framebufferInfo.height = extent.height;
      framebufferInfo.layers = 1;

      vkCheck(vkCreateFramebuffer(ctx.device, &framebufferInfo, nullptr,
                                  &framebuffers[i]),
              "Failed to create framebuffer");
    }
  }
}

auto Swapchain::findSupportedFormat(
    const std::vector<VkFormat> &candidates, VkImageTiling tiling,
    VkFormatFeatureFlags features) const -> VkFormat {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL &&
        (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("Failed to find supported format!");
}

auto Swapchain::findDepthFormat() const -> VkFormat {
  return findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void Swapchain::createDepthResources() {
  depthFormat = findDepthFormat();

  // One depth image per swap-chain image. Each acquired image index is owned
  // by exactly one in-flight frame at a time, so keying depth off the image
  // index keeps overlapping frames from racing on a shared depth buffer.
  depthImages.assign(images.size(), VK_NULL_HANDLE);
  depthAllocations.assign(images.size(), VK_NULL_HANDLE);
  depthImageViews.assign(images.size(), VK_NULL_HANDLE);

  for (size_t i = 0; i < images.size(); i++) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vkCheck(vmaCreateImage(ctx.allocator, &imageInfo, &allocCreateInfo,
                           &depthImages[i], &depthAllocations[i], nullptr),
            "Failed to create depth image");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vkCheck(
        vkCreateImageView(ctx.device, &viewInfo, nullptr, &depthImageViews[i]),
        "Failed to create depth image view");
  }
}

void Swapchain::destroyImageResources() {
  for (auto framebuffer : framebuffers) {
    vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
  }
  framebuffers.clear();

  for (auto imageView : imageViews) {
    vkDestroyImageView(ctx.device, imageView, nullptr);
  }
  imageViews.clear();

  for (auto depthView : depthImageViews) {
    if (depthView != VK_NULL_HANDLE) {
      vkDestroyImageView(ctx.device, depthView, nullptr);
    }
  }
  depthImageViews.clear();

  // depthImages and depthAllocations stay in lockstep; vmaDestroyImage is a
  // no-op on VK_NULL_HANDLE, so a partially-constructed set (after a throw
  // mid-createDepthResources) tears down safely.
  for (size_t i = 0; i < depthImages.size(); i++) {
    vmaDestroyImage(ctx.allocator, depthImages[i], depthAllocations[i]);
  }
  depthImages.clear();
  depthAllocations.clear();
}

void Swapchain::cleanup() {
  if (ctx.device == VK_NULL_HANDLE) return;

  destroyImageResources();

  if (renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(ctx.device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
  }

  if (swapChain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(ctx.device, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }
}
