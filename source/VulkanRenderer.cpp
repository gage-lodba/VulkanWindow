#include "VulkanRenderer.h"

#include <array>
#include <cstdint>
#include <stdexcept>

#include "ImGuiManager.h"
#include "Swapchain.h"
#include "VulkanContext.h"
#include "VulkanUtils.h"

using vkutil::vkCheck;

VulkanRenderer::VulkanRenderer(GLFWwindow *window, PresentMode presentMode,
                               uint32_t framesInFlight)
    : window(window),
      preferredPresentMode(presentMode),
      framesInFlight(framesInFlight) {
  if (framesInFlight == 0) {
    throw std::invalid_argument("framesInFlight must be > 0");
  }
  try {
    context = std::make_unique<VulkanContext>(window,
                                              enableBestPracticesValidation);
    swapchain =
        std::make_unique<Swapchain>(*context, window, preferredPresentMode);
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createImGui();
  } catch (...) {
    cleanup();
    throw;
  }
}

VulkanRenderer::~VulkanRenderer() { cleanup(); }

void VulkanRenderer::waitIdle() const {
  if (context && context->device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(context->device);
  }
}

void VulkanRenderer::createCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = context->graphicsQueueFamily;

  vkCheck(
      vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool),
      "Failed to create command pool");
}

void VulkanRenderer::createCommandBuffers() {
  commandBuffers.resize(framesInFlight);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

  vkCheck(vkAllocateCommandBuffers(context->device, &allocInfo,
                                   commandBuffers.data()),
          "Failed to allocate command buffers");
}

void VulkanRenderer::createSyncObjects() {
  imageAvailableSemaphores.assign(framesInFlight, VK_NULL_HANDLE);
  inFlightFences.assign(framesInFlight, VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < framesInFlight; i++) {
    vkCheck(vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                              &imageAvailableSemaphores[i]),
            "Failed to create image-available semaphore");
    vkCheck(vkCreateFence(context->device, &fenceInfo, nullptr,
                          &inFlightFences[i]),
            "Failed to create in-flight fence");
  }

  createPerImageSemaphores();
}

void VulkanRenderer::createPerImageSemaphores() {
  // One render-finished semaphore per swapchain image, since the present
  // engine holds the semaphore until the image is re-acquired.
  renderFinishedSemaphores.assign(swapchain->images.size(), VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  for (size_t i = 0; i < swapchain->images.size(); i++) {
    vkCheck(vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                              &renderFinishedSemaphores[i]),
            "Failed to create render-finished semaphore");
  }
}

void VulkanRenderer::destroyRenderFinishedSemaphores() {
  for (auto &semaphore : renderFinishedSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(context->device, semaphore, nullptr);
    }
  }
  renderFinishedSemaphores.clear();
}

void VulkanRenderer::recreateImageAvailableSemaphores() {
  for (auto &semaphore : imageAvailableSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(context->device, semaphore, nullptr);
      semaphore = VK_NULL_HANDLE;
    }
  }

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (auto &semaphore : imageAvailableSemaphores) {
    vkCheck(vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                              &semaphore),
            "Failed to recreate image-available semaphore");
  }
}

void VulkanRenderer::createImGui() {
  // Pass the user's style callback into the constructor so the default
  // dark theme isn't applied first and then immediately overwritten.
  imguiManager = std::make_unique<ImGuiManager>(
      window, context->instanceApiVersion, context->instance,
      context->physicalDevice, context->device, context->graphicsQueueFamily,
      context->graphicsQueue, swapchain->renderPass, context->pipelineCache,
      swapchain->minImageCount,
      static_cast<uint32_t>(swapchain->images.size()), 100, styleCallback);
}

void VulkanRenderer::setPresentMode(PresentMode mode) {
  if (mode == preferredPresentMode) return;
  preferredPresentMode = mode;
  // Force a rebuild on the next frame. recreateSwapChain re-queries the
  // surface and runs chooseSwapPresentMode with the new preference.
  framebufferResized = true;
}

void VulkanRenderer::setUICallback(std::function<void()> callback) {
  uiCallback = std::move(callback);
}

void VulkanRenderer::setRenderCallback(
    std::function<void(VkCommandBuffer, VkExtent2D)> callback) {
  renderCallback = std::move(callback);
}

void VulkanRenderer::setStyleCallback(std::function<void()> callback) {
  styleCallback = std::move(callback);
  if (imguiManager) {
    imguiManager->setStyleCallback(styleCallback);
  }
}

void VulkanRenderer::setSwapchainRecreatedCallback(
    std::function<void(const SwapchainRecreateInfo &)> callback) {
  swapchainRecreatedCallback = std::move(callback);
}

void VulkanRenderer::setClearColor(float r, float g, float b,
                                   float a) noexcept {
  clearColor = {r, g, b, a};
}

auto VulkanRenderer::getContext() const noexcept -> const VulkanContext & {
  return *context;
}

auto VulkanRenderer::getSwapchain() const noexcept -> const Swapchain & {
  return *swapchain;
}

void VulkanRenderer::drawFrame() {
  // Application::run() already gates on a 0×0 framebuffer and blocks on
  // waitEvents while minimized, so we don't re-check here.
  vkWaitForFences(context->device, 1, &inFlightFences[currentFrame], VK_TRUE,
                  UINT64_MAX);

  uint32_t imageIndex = 0;
  VkResult result = vkAcquireNextImageKHR(
      context->device, swapchain->swapChain, UINT64_MAX,
      imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    // Per spec the supplied semaphore is now indeterminate — flag it so
    // recreateSwapChain rebuilds it instead of leaving a stale handle.
    acquireSemaphoresInvalid = true;
    recreateSwapChain();
    return;
  }
  if (result == VK_SUBOPTIMAL_KHR) {
    // The image was still acquired and the semaphore signaled, so finish the
    // frame with the suboptimal swapchain. Force a recreate after present so
    // we don't depend on the present call also reporting suboptimal.
    framebufferResized = true;
  } else if (result != VK_SUCCESS) {
    vkCheck(result, "Failed to acquire swap chain image");
  }

  vkCheck(vkResetCommandBuffer(commandBuffers[currentFrame], 0),
          "Failed to reset command buffer");

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  vkCheck(vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo),
          "Failed to begin recording command buffer");

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = swapchain->renderPass;
  renderPassInfo.renderArea.offset = {.x = 0, .y = 0};
  renderPassInfo.renderArea.extent = swapchain->extent;

  VkClearValue clearColorValue{};
  clearColorValue.color = {{clearColor[0], clearColor[1], clearColor[2],
                            clearColor[3]}};
  VkClearValue clearDepth{};
  clearDepth.depthStencil = {.depth = 1.0f, .stencil = 0};
  std::array clearValues = {clearColorValue, clearDepth};
  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  // For imageless framebuffers, provide image views at begin time.
  std::array imagelessAttachments = {swapchain->imageViews[imageIndex],
                                     swapchain->depthImageViews[imageIndex]};
  VkRenderPassAttachmentBeginInfo attachmentBeginInfo{};
  if (context->useImagelessFramebuffer) {
    attachmentBeginInfo.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
    attachmentBeginInfo.attachmentCount =
        static_cast<uint32_t>(imagelessAttachments.size());
    attachmentBeginInfo.pAttachments = imagelessAttachments.data();
    renderPassInfo.pNext = &attachmentBeginInfo;
    renderPassInfo.framebuffer = swapchain->framebuffers[0];
  } else {
    renderPassInfo.framebuffer = swapchain->framebuffers[imageIndex];
  }

  vkCmdBeginRenderPass(commandBuffers[currentFrame], &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  // User render callback (geometry, etc.) runs inside the render pass, before
  // ImGui — so ImGui composites on top.
  if (renderCallback) {
    renderCallback(commandBuffers[currentFrame], swapchain->extent);
  }

  // Start ImGui frame
  imguiManager->newFrame();

  // Invoke the user-provided UI callback (if any)
  if (uiCallback) {
    uiCallback();
  }

  // Record ImGui draw commands
  imguiManager->render(commandBuffers[currentFrame]);

  vkCmdEndRenderPass(commandBuffers[currentFrame]);

  vkCheck(vkEndCommandBuffer(commandBuffers[currentFrame]),
          "Failed to record command buffer");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  std::array waitSemaphores = {imageAvailableSemaphores[currentFrame]};
  std::array<VkPipelineStageFlags, 1> waitStages = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores.data();
  submitInfo.pWaitDstStageMask = waitStages.data();
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

  std::array signalSemaphores = {renderFinishedSemaphores[imageIndex]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores.data();

  // Reset the fence as late as possible — directly before submit — so the
  // window in which the fence is unsignaled is minimal. If submit fails the
  // fence stays unsignaled, but we throw on that path and the application
  // terminates without ever waiting on it again.
  vkResetFences(context->device, 1, &inFlightFences[currentFrame]);

  // The exception propagates out of run() and the application terminates,
  // so there's no point trying to rebuild sync objects for a frame that
  // will never be retried. cleanup() handles destruction.
  vkCheck(vkQueueSubmit(context->graphicsQueue, 1, &submitInfo,
                        inFlightFences[currentFrame]),
          "Failed to submit draw command buffer");

  // Multi-viewport: spawn/update/render secondary platform windows. Each
  // viewport's swap-chain and submit/present is handled internally by the
  // ImGui Vulkan backend. No-op when the docking branch isn't in use.
  imguiManager->renderPlatformWindows();

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores.data();

  std::array swapChains = {swapchain->swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains.data();
  presentInfo.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(context->presentQueue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (result != VK_SUCCESS) {
    vkCheck(result, "Failed to present swap chain image");
  }

  currentFrame = (currentFrame + 1) % framesInFlight;
}

void VulkanRenderer::recreateSwapChain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  // Defer recreation while the window is minimized (0×0 framebuffer).
  // The resize callback will trigger another attempt when restored.
  if (width == 0 || height == 0) return;

  // Clear the resize signal now that we've committed to rebuilding. The
  // 0×0 early-return above leaves it set so the next drawFrame retries the
  // rebuild; clearing it here (rather than at the end) means a throw
  // mid-rebuild won't leave a stale flag — though in practice we terminate
  // on throw, so this is correctness-by-construction more than a real bug.
  framebufferResized = false;

  vkDeviceWaitIdle(context->device);

  // Destroy old per-image render-finished semaphores. The helper clears the
  // vector immediately so a throw before recreation doesn't leave dangling
  // handles for the destructor to free a second time.
  destroyRenderFinishedSemaphores();

  // Per Vulkan spec, when vkAcquireNextImageKHR returns
  // VK_ERROR_OUT_OF_DATE_KHR the supplied semaphore is left in an indeterminate
  // state — it cannot be safely reused or waited on. Only on that path
  // (signalled via acquireSemaphoresInvalid) do we tear down and recreate; on
  // the suboptimal / framebufferResized paths the semaphore was used and
  // unsignaled normally.
  if (acquireSemaphoresInvalid) {
    recreateImageAvailableSemaphores();
    acquireSemaphoresInvalid = false;
  }

  const uint32_t oldMinImageCount = swapchain->minImageCount;

  const Swapchain::RecreateResult res =
      swapchain->recreate(preferredPresentMode);

  // ImGui's Vulkan backend pins the render pass and ImageCount at init. It
  // only needs a teardown + rebuild when the format (→ new render pass) or
  // image count changed; pure extent changes are fine. The old render pass
  // (if any) is held by `res.oldRenderPassToDestroy` for us to destroy after
  // ImGui has been torn down.
  const bool needFullImGuiRebuild =
      res.formatChanged || res.imageCountChanged;

  if (needFullImGuiRebuild) {
    imguiManager.reset();
  }

  if (res.oldRenderPassToDestroy != VK_NULL_HANDLE) {
    vkDestroyRenderPass(context->device, res.oldRenderPassToDestroy, nullptr);
  }

  if (needFullImGuiRebuild) {
    createImGui();
  } else if (swapchain->minImageCount != oldMinImageCount && imguiManager) {
    imguiManager->setMinImageCount(swapchain->minImageCount);
  }

  // Recreate per-image render-finished semaphores for the new swap chain
  createPerImageSemaphores();

  // Notify the app last — all new swap-chain state is in place and the device
  // is still idle (vkDeviceWaitIdle above, no work submitted since), so the
  // callback can safely destroy and rebuild format-/extent-dependent
  // pipelines and resources.
  if (swapchainRecreatedCallback) {
    swapchainRecreatedCallback({.formatChanged = res.formatChanged,
                                .imageCountChanged = res.imageCountChanged,
                                .extent = swapchain->extent});
  }
}

void VulkanRenderer::cleanup() {
  waitIdle();

  imguiManager.reset();

  if (context && context->device != VK_NULL_HANDLE) {
    destroyRenderFinishedSemaphores();
    for (auto semaphore : imageAvailableSemaphores) {
      if (semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(context->device, semaphore, nullptr);
      }
    }
    for (auto fence : inFlightFences) {
      if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(context->device, fence, nullptr);
      }
    }

    if (commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(context->device, commandPool, nullptr);
      commandPool = VK_NULL_HANDLE;
    }
  }

  // Swapchain destructor handles its own resources (swap-chain, image views,
  // depth, render pass, framebuffers).
  swapchain.reset();

  // VulkanContext destructor handles allocator/device/surface/instance/etc.
  context.reset();
}
