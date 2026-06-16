# Textured-quad example — exercises the full helper surface (vertex buffer,
# mipmapped texture, sampler, descriptor set, embedded shaders, pipeline).
# Needs glslc, like the triangle example; skips itself otherwise.

if(NOT VULKANWINDOW_GLSLC_FOUND)
  message(STATUS
    "VulkanWindow: glslc not found — skipping the textured_quad example.")
  return()
endif()

add_executable(VulkanWindowTexturedQuad main.cpp)
vulkanwindow_add_shaders(VulkanWindowTexturedQuad
  "${CMAKE_CURRENT_SOURCE_DIR}/quad.vert"
  "${CMAKE_CURRENT_SOURCE_DIR}/quad.frag")
target_link_libraries(VulkanWindowTexturedQuad
  PRIVATE VulkanWindow::VulkanWindow)

if(MSVC)
  target_compile_options(VulkanWindowTexturedQuad PRIVATE /W4)
else()
  target_compile_options(VulkanWindowTexturedQuad PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  set_property(TARGET VulkanWindowTexturedQuad PROPERTY
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
  if(NOT MINGW)
    target_link_options(VulkanWindowTexturedQuad PRIVATE
      $<$<NOT:$<CONFIG:Debug>>:LINKER:/ENTRY:mainCRTStartup>)
  endif()
endif()
