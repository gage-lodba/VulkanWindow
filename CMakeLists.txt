cmake_minimum_required(VERSION 3.11)
project(VulkanWindow)

# Vulkan
find_package(Vulkan REQUIRED)

# Glfw
add_subdirectory(glfw)
include_directories(glfw/include)

# ImGui
include_directories(imgui/ imgui/backends imgui/misc/cpp ..)

file(
  GLOB
  sources
  source/**.cpp
  source/**.hpp
  imgui/imgui.cpp
  imgui/imgui.h
  imgui/imgui_draw.cpp
  imgui/imgui_demo.cpp
  imgui/imgui_internal.h
  imgui/imgui_tables.cpp
  imgui/imgui_widgets.cpp
  imgui/backends/imgui_impl_vulkan.cpp
  imgui/backends/imgui_impl_vulkan.h
  imgui/backends/imgui_impl_glfw.cpp
  imgui/backends/imgui_impl_glfw.h)

add_executable(VulkanWindow)

target_sources(VulkanWindow PRIVATE ${sources})
target_link_libraries(VulkanWindow PUBLIC glfw Vulkan::Vulkan)
target_compile_features(VulkanWindow PRIVATE cxx_std_20)

if(WIN32)
  set_property(TARGET VulkanWindow PROPERTY WIN32_EXECUTABLE true)
endif()
