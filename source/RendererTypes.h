#pragma once

// Lightweight, dependency-free enums shared across the renderer's public
// surface (Application, VulkanRenderer, Swapchain). Kept in their own header so
// Application.h can name them in default arguments without pulling in the
// GLFW/Vulkan includes that VulkanRenderer.h drags in.

/// Preferred swap-chain present mode. The renderer falls back to
/// VK_PRESENT_MODE_FIFO_KHR (vsync, always supported) when the preferred mode
/// isn't available.
enum class PresentMode {
  /// VK_PRESENT_MODE_FIFO_KHR — vsync. Lowest GPU/power usage. Default.
  Vsync,
  /// VK_PRESENT_MODE_MAILBOX_KHR — tear-free, lower latency than FIFO. Burns
  /// GPU rendering frames that are then discarded.
  Mailbox,
  /// VK_PRESENT_MODE_IMMEDIATE_KHR — no vsync. Tearing, lowest latency.
  Immediate,
};

/// Preferred swap-chain surface format (colour encoding). Chosen once at
/// construction; read the format actually picked from
/// `Application::getSwapchain().imageFormat`.
enum class SurfaceFormatPreference {
  /// Prefer an 8-bit UNORM surface (default). Colours written to the swap-chain
  /// — ImGui's vertex colours and `setClearColor` — are presented verbatim with
  /// no gamma conversion. This is the path Dear ImGui is designed for (it does
  /// not gamma-correct its output), so the built-in theme and any colours you
  /// author display exactly as specified. Best for UI-centric apps.
  Unorm,
  /// Prefer an 8-bit sRGB surface. The GPU gamma-encodes (linear → sRGB) on
  /// write, which is correct for linear-space 3D lighting and blending. ImGui
  /// authors its colours in sRGB, so they must be linearised first: the
  /// scaffold linearises its own built-in theme for you, but your `ImGuiCol_*`
  /// / `PushStyleColor` values and `setClearColor` are treated as linear — run
  /// sRGB values through `vkutil::srgbToLinear` yourself.
  Srgb,
};
