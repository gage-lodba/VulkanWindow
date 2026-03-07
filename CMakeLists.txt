cmake_minimum_required(VERSION 3.12)
project(VulkanWindow)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS ON)

# Vulkan
find_package(Vulkan REQUIRED)

# VulkanMemoryAllocator
add_subdirectory(deps/VulkanMemoryAllocator)

# Glfw
add_subdirectory(deps/GLFW)

# Library sources
set(lib_sources
  source/Application.cpp
  source/Application.h
  source/ImGuiManager.cpp
  source/ImGuiManager.h
  source/UserInterface.cpp
  source/UserInterface.h
  source/VulkanRenderer.cpp
  source/VulkanRenderer.h
  source/Window.cpp
  source/Window.h
  source/vma_impl.cpp
  deps/ImGui/imgui.cpp
  deps/ImGui/imgui.h
  deps/ImGui/imgui_draw.cpp
  deps/ImGui/imgui_internal.h
  deps/ImGui/imgui_tables.cpp
  deps/ImGui/imgui_widgets.cpp
  # imgui_demo.cpp is intentionally excluded
  deps/ImGui/backends/imgui_impl_vulkan.cpp
  deps/ImGui/backends/imgui_impl_vulkan.h
  deps/ImGui/backends/imgui_impl_glfw.cpp
  deps/ImGui/backends/imgui_impl_glfw.h)

# Static library target
add_library(VulkanWindowLib STATIC)

target_sources(VulkanWindowLib PRIVATE ${lib_sources})
target_include_directories(VulkanWindowLib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/GLFW/include
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/backends
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/misc/cpp)
# Mark third-party VMA headers as SYSTEM to suppress warnings
target_include_directories(VulkanWindowLib SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/deps/VulkanMemoryAllocator/include)
target_link_libraries(VulkanWindowLib PUBLIC glfw Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)

# Compiler warnings
if(MSVC)
  target_compile_options(VulkanWindowLib PRIVATE /W4)
else()
  target_compile_options(VulkanWindowLib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Only build the demo executable when this is the top-level project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(VulkanWindow source/main.cpp)
  target_link_libraries(VulkanWindow PRIVATE VulkanWindowLib)

  # Hide console window in release builds on Windows
  if(WIN32)
    set_property(TARGET VulkanWindow PROPERTY WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    if(NOT MINGW)
      target_link_options(VulkanWindow PRIVATE $<$<NOT:$<CONFIG:Debug>>:LINKER:/ENTRY:mainCRTStartup>)
    endif()
  endif()
endif()
