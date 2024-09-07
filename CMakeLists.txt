cmake_minimum_required(VERSION 3.11)
project(VulkanWindow C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Vulkan REQUIRED)

add_subdirectory(glfw)
include_directories(${GLFW_DIR}/deps)

set(IMGUI_DIR imgui/)
include_directories(imgui/ imgui/backends imgui/misc/cpp ..)

add_executable(VulkanWindow)

target_sources(
  VulkanWindow
  PRIVATE source/main.cpp
          source/application.cpp
          source/application.hpp
          imgui/imgui.cpp
          imgui/imgui.h
          imgui/imgui_draw.cpp
          imgui/imgui_internal.h
          imgui/imgui_tables.cpp
          imgui/imgui_widgets.cpp
          imgui/backends/imgui_impl_vulkan.cpp
          imgui/backends/imgui_impl_vulkan.h
          imgui/backends/imgui_impl_glfw.cpp
          imgui/backends/imgui_impl_glfw.h)
target_link_libraries(VulkanWindow PUBLIC glfw Vulkan::Vulkan)

if(WIN32)
  set_property(TARGET VulkanWindow PROPERTY WIN32_EXECUTABLE true)
endif()
