cmake_minimum_required(VERSION 3.11)
project(VulkanWindow)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# Glfw
add_subdirectory(deps/glfw)
include_directories(deps/glfw/include)

# ImGui
include_directories(deps/imgui deps/imgui/backends deps/imgui/misc/cpp)

file(
  GLOB
  sources
  source/**.cpp
  source/**.h
  deps/imgui/imgui.cpp
  deps/imgui/imgui.h
  deps/imgui/imgui_draw.cpp
  deps/imgui/imgui_internal.h
  deps/imgui/imgui_tables.cpp
  deps/imgui/imgui_widgets.cpp
  deps/imgui/backends/imgui_impl_vulkan.cpp
  deps/imgui/backends/imgui_impl_vulkan.h
  deps/imgui/backends/imgui_impl_glfw.cpp
  deps/imgui/backends/imgui_impl_glfw.h)

add_executable(VulkanWindow)

target_sources(VulkanWindow PRIVATE ${sources})
target_link_libraries(VulkanWindow PRIVATE glfw Vulkan::Vulkan)

if(WIN32)
  set_source_files_properties(source/Main.cpp PROPERTIES COMPILE_DEFINITIONS main=WinMain)
  set_property(TARGET VulkanWindow PROPERTY WIN32_EXECUTABLE true)
endif()
