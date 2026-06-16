# Triangle example — builds a VkPipeline from GLSL compiled to SPIR-V and draws
# it through Application::setRenderCallback.
#
# Shader compilation + embedding is done by vulkanwindow_add_shaders() (from
# cmake/VulkanWindowShaders.cmake), the same helper a consumer would use. It
# needs glslc (Vulkan SDK); when it isn't found the example skips itself with a
# message rather than failing the build.

if(NOT VULKANWINDOW_GLSLC_FOUND)
  message(STATUS
    "VulkanWindow: glslc not found — skipping the triangle example. "
    "Install the Vulkan SDK (or set VULKANWINDOW_GLSLC) to build it.")
  return()
endif()

add_executable(VulkanWindowTriangle main.cpp)

# Compile triangle.vert/.frag to embeddable SPIR-V and wire them into the
# target (generated *.spv.inc on the include path; main.cpp #includes them).
vulkanwindow_add_shaders(VulkanWindowTriangle
  "${CMAKE_CURRENT_SOURCE_DIR}/triangle.vert"
  "${CMAKE_CURRENT_SOURCE_DIR}/triangle.frag")

target_link_libraries(VulkanWindowTriangle PRIVATE VulkanWindow::VulkanWindow)

if(MSVC)
  target_compile_options(VulkanWindowTriangle PRIVATE /W4)
else()
  target_compile_options(VulkanWindowTriangle PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Mirror the demo: hide the console on Windows non-Debug builds.
if(WIN32)
  set_property(TARGET VulkanWindowTriangle PROPERTY
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
  if(NOT MINGW)
    target_link_options(VulkanWindowTriangle PRIVATE
      $<$<NOT:$<CONFIG:Debug>>:LINKER:/ENTRY:mainCRTStartup>)
  endif()
endif()
