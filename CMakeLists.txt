cmake_minimum_required(VERSION 3.11)
project(VulkanWindow C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Vulkan REQUIRED)

add_subdirectory(glfw)
include_directories(${GLFW_DIR}/deps)

set(IMGUI_DIR imgui/)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp ..)

file(GLOB sources
  source/*
  imgui/*
  imgui/backends/imgui_impl_vulkan.cpp
  imgui/backends/imgui_impl_vulkan.h
  imgui/backends/imgui_impl_glfw.cpp
  imgui/backends/imgui_impl_glfw.h
)

add_executable(VulkanWindow ${sources})

target_link_libraries(VulkanWindow PUBLIC glfw;Vulkan::Vulkan)