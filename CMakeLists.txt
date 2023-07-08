cmake_minimum_required(VERSION 3.15)
project(VulkanWindow C CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Vulkan REQUIRED)

include(scripts/glfw.cmake)
include(scripts/imgui.cmake)

file(GLOB_RECURSE src_main
    ${PROJECT_SOURCE_DIR}/source/*.hpp
    ${PROJECT_SOURCE_DIR}/source/*.cpp
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include_directories(${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ..)
include_directories(${glfw_SOURCE_DIR}/deps)

add_executable(VulkanWindow ${src_main}
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_link_libraries(VulkanWindow PUBLIC
    glfw
    Vulkan::Vulkan
)
