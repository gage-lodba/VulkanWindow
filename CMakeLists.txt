cmake_minimum_required(VERSION 3.11)
project(VulkanWindow)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# Glfw
add_subdirectory(deps/GLFW)
include_directories(deps/GLFW/include)

# ImGui
include_directories(deps/ImGui deps/ImGui/backends deps/ImGui/misc/cpp)

file(
  GLOB
  sources
  source/**.cpp
  source/**.h
  deps/ImGui/imgui.cpp
  deps/ImGui/imgui.h
  deps/ImGui/imgui_draw.cpp
  deps/ImGui/imgui_internal.h
  deps/ImGui/imgui_tables.cpp
  deps/ImGui/imgui_widgets.cpp
  deps/ImGui/backends/imgui_impl_vulkan.cpp
  deps/ImGui/backends/imgui_impl_vulkan.h
  deps/ImGui/backends/imgui_impl_glfw.cpp
  deps/ImGui/backends/imgui_impl_glfw.h)

add_executable(VulkanWindow)

target_sources(VulkanWindow PRIVATE ${sources})
target_link_libraries(VulkanWindow PRIVATE glfw Vulkan::Vulkan)

# if windows and not debug build
if(WIN32 AND NOT(CMAKE_BUILD_TYPE STREQUAL "Debug"))
  set_source_files_properties(source/Main.cpp PROPERTIES COMPILE_DEFINITIONS main=WinMain)
  set_property(TARGET VulkanWindow PROPERTY WIN32_EXECUTABLE true)
endif()
