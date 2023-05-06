# Minimum cmake version
cmake_minimum_required(VERSION "3.26.3")

# Compiler options
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile library
project(VulkanWindow)

# Setting source directory
set(SRC_DIR "${PROJECT_SOURCE_DIR}/source")

# Fetch dependencies
include(scripts/imgui.cmake)
include(scripts/glfw.cmake)

# Locate the vulkan package
find_package(Vulkan REQUIRED)

# Get files
file(GLOB_RECURSE SRC_MAIN
    "${SRC_DIR}/**.hpp"
    "${SRC_DIR}/**.h"
    "${SRC_DIR}/**.cpp"
)

# Define library target
add_executable(VulkanWindow "${SRC_MAIN}")

# Target include directories
target_include_directories(VulkanWindow PRIVATE
    "${SRC_DIR}"
    "${Vulkan_INCLUDE_DIRS}"
    "${imgui_SOURCE_DIR}"
    "${glfw_SOURCE_DIR}"
)

# Target precompile headers
# target_precompile_headers(VulkanWindow PRIVATE "${SRC_DIR}/common.hpp")

# Target link libraries
target_link_libraries(VulkanWindow PRIVATE imgui glfw)

# Add compile definitions
if (WIN32)
    add_compile_definitions(VulkanWindow 
    "_CRT_SECURE_NO_WARNINGS"
    "NOMINMAX"
    "WIN32_LEAN_AND_MEAN"
    )
endif()