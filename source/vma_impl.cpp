// VulkanMemoryAllocator (vendored, pinned v3.4.0) implementation, compiled
// exactly once — VMA's integration docs require VMA_IMPLEMENTATION in a single
// C++ translation unit. VMA's defaults are deliberately kept (no
// VMA_*FUNCTIONS overrides): with prototypes available VMA binds the directly
// linked loader symbols statically, and its default dynamic path
// (vkGetInstanceProcAddr/vkGetDeviceProcAddr) fills in anything else. That also
// keeps consumers that build with VK_NO_PROTOTYPES (e.g. volk) working, since
// VMA then switches to dynamic binding on its own. VmaAllocatorCreateInfo in
// VulkanContext supplies instance/physicalDevice/device/apiVersion.
//
// The clang diagnostics below are suppressed for VMA's own
// header/implementation code only (the C API exposes incomplete nullability
// annotations, and the C++ wrapper stores private fields not every
// configuration uses). Nothing in this project's own code is covered by these
// pragmas.

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif
