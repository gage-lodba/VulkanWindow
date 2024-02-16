cmake_minimum_required(VERSION 3.11)
project(VulkanWindow C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Vulkan REQUIRED)

add_subdirectory(glfw)

set(IMGUI_DIR imgui/)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends ..)

add_executable(VulkanWindow source/main.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)

target_link_libraries(VulkanWindow PUBLIC
  Vulkan::Vulkan
  glfw
)