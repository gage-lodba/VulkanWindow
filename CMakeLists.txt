cmake_minimum_required(VERSION 3.12)
project(VulkanWindow VERSION 0.1.0)

# True iff this CMakeLists is the top-level build, not a child of someone
# else's add_subdirectory(). Used to gate settings that should only affect
# stand-alone builds — never the parent project.
set(VULKANWINDOW_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(VULKANWINDOW_IS_TOP_LEVEL ON)
endif()

if(VULKANWINDOW_IS_TOP_LEVEL)
  # Always emit compile_commands.json for clangd/clang-tidy. Top-level only
  # so we don't override a consuming project's tooling preference.
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  # Default to Release on single-config generators when no build type is
  # given. Top-level only so we never force a build type on the parent.
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
endif()

# Project options
option(VULKANWINDOW_BUILD_DEMO
  "Build the VulkanWindow demo executable"
  ${VULKANWINDOW_IS_TOP_LEVEL})
option(VULKANWINDOW_BUILD_EXAMPLES
  "Build the example executables (needs glslc from the Vulkan SDK)"
  ${VULKANWINDOW_IS_TOP_LEVEL})
option(VULKANWINDOW_BEST_PRACTICES
  "Enable Vulkan best-practices validation in Debug builds"
  OFF)
# When ON, install rules are emitted AND glfw3 / VulkanMemoryAllocator are
# resolved via find_package instead of the vendored submodules. Off by
# default because the bundled-submodule build is the dev-friendly default;
# installation is a vcpkg/Conan/system-packager concern.
option(VULKANWINDOW_INSTALL
  "Generate install rules. Requires find_package(glfw3, VulkanMemoryAllocator)."
  OFF)

# Vulkan
find_package(Vulkan REQUIRED)

# Shader helper: defines vulkanwindow_add_shaders() and locates glslc. Made
# available to consumers via add_subdirectory/FetchContent (CMake functions are
# global once defined) and via the installed package config below.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanWindowShaders.cmake")

# VulkanMemoryAllocator — when install is ON, require a system/find_package
# version so the export set can reference it cleanly. Otherwise, fall back to
# the vendored submodule (which doesn't ship its own install rules in a way
# our export can consume).
if(VULKANWINDOW_INSTALL)
  if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
  endif()
elseif(NOT TARGET GPUOpen::VulkanMemoryAllocator)
  add_subdirectory(deps/VulkanMemoryAllocator)
endif()

# GLFW — same story. With install ON we require find_package(glfw3); without,
# we use the submodule. We only seed our preferred GLFW cache defaults when
# GLFW isn't already a target *and* the values aren't already cached.
if(VULKANWINDOW_INSTALL)
  if(NOT TARGET glfw)
    find_package(glfw3 CONFIG REQUIRED)
  endif()
elseif(NOT TARGET glfw)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
  set(GLFW_INSTALL OFF CACHE BOOL "")
  add_subdirectory(deps/GLFW)
endif()

# ImGui — compiled as a separate static library so a parent project that
# already brings its own ImGui (own add_library, FetchContent, etc.) can
# provide it instead of getting duplicate-symbol link errors. If the parent
# defines an `imgui` target before add_subdirectory'ing us, that target is
# used as-is and must include the Vulkan and GLFW backends compatible with
# our renderer (see VulkanRenderer.cpp PipelineInfoMain usage — Dear ImGui
# >= 1.92.2).
if(NOT TARGET imgui)
  add_library(imgui STATIC
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
  target_include_directories(imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/ImGui/backends>
    $<INSTALL_INTERFACE:include/VulkanWindow/imgui>
    $<INSTALL_INTERFACE:include/VulkanWindow/imgui/backends>)
  # Backends need GLFW headers and Vulkan; pinning these PUBLIC means
  # consumers linking imgui get the transitive deps for free.
  target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
endif()

# Library sources
set(lib_sources
  source/Application.cpp
  source/Application.h
  source/ImGuiManager.cpp
  source/ImGuiManager.h
  source/VulkanDescriptors.cpp
  source/VulkanDescriptors.h
  source/Swapchain.cpp
  source/Swapchain.h
  source/UserInterface.cpp
  source/UserInterface.h
  source/VulkanContext.cpp
  source/VulkanContext.h
  source/VulkanRenderer.cpp
  source/VulkanRenderer.h
  source/VulkanResources.cpp
  source/VulkanResources.h
  source/VulkanUtils.cpp
  source/VulkanUtils.h
  source/Window.cpp
  source/Window.h
  source/vma_impl.cpp)

# Static library target. The target is named `VulkanWindow`; consumers
# should reference it via the namespaced alias `VulkanWindow::VulkanWindow`
# so the same call site works whether the lib was pulled in via
# add_subdirectory, FetchContent, or (in the future) an installed package.
add_library(VulkanWindow STATIC)
add_library(VulkanWindow::VulkanWindow ALIAS VulkanWindow)

target_sources(VulkanWindow PRIVATE ${lib_sources})
# Wrap source paths in BUILD_INTERFACE so they apply to in-tree consumers
# but not to any future install() of this target — installed consumers
# would get a relative include path instead of one referencing this
# build-tree's absolute paths. ImGui include dirs propagate transitively
# from the `imgui` target via the PUBLIC link below.
target_include_directories(VulkanWindow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
  $<INSTALL_INTERFACE:include/VulkanWindow>)
# Mark third-party VMA headers as SYSTEM to suppress warnings
target_include_directories(VulkanWindow SYSTEM PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/VulkanMemoryAllocator/include>)
target_link_libraries(VulkanWindow PUBLIC
  glfw
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
  imgui)
# Require C++20 at the target level so consumers inherit the requirement
# without us forcing CMAKE_CXX_STANDARD on the parent project.
target_compile_features(VulkanWindow PUBLIC cxx_std_20)

# Compiler warnings
if(MSVC)
  target_compile_options(VulkanWindow PRIVATE /W4)
else()
  target_compile_options(VulkanWindow PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optional best-practices validation (read by VulkanRenderer.h to enable
# VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT in Debug builds).
if(VULKANWINDOW_BEST_PRACTICES)
  target_compile_definitions(VulkanWindow PRIVATE VULKAN_WINDOW_BEST_PRACTICES)
endif()

if(VULKANWINDOW_BUILD_DEMO)
  # Demo executable. Target name is VulkanWindowDemo so it doesn't collide
  # with the library; OUTPUT_NAME keeps the produced binary file named
  # VulkanWindow(.exe) so existing tooling (CI, packaging) still finds it.
  add_executable(VulkanWindowDemo source/main.cpp)
  set_target_properties(VulkanWindowDemo PROPERTIES OUTPUT_NAME VulkanWindow)
  target_link_libraries(VulkanWindowDemo PRIVATE VulkanWindow::VulkanWindow)

  # Hide console window in release builds on Windows
  if(WIN32)
    set_property(TARGET VulkanWindowDemo PROPERTY WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    if(NOT MINGW)
      target_link_options(VulkanWindowDemo PRIVATE $<$<NOT:$<CONFIG:Debug>>:LINKER:/ENTRY:mainCRTStartup>)
    endif()
  endif()
endif()

# Example executables (top-level builds only by default). Each example is
# self-contained under examples/; the triangle one compiles its GLSL via glslc
# and skips itself gracefully when glslc isn't available.
if(VULKANWINDOW_BUILD_EXAMPLES)
  add_subdirectory(examples/triangle)
  add_subdirectory(examples/textured_quad)
endif()

# ---- Install / export -------------------------------------------------------
#
# When VULKANWINDOW_INSTALL is ON, generate install rules that lay out the
# project as a findable CMake package:
#
#   <prefix>/include/VulkanWindow/*.h          public headers
#   <prefix>/include/VulkanWindow/imgui/...    Dear ImGui headers (bundled)
#   <prefix>/lib/libVulkanWindow.a             static library
#   <prefix>/lib/libimgui.a                    bundled imgui static library
#   <prefix>/lib/cmake/VulkanWindow/...        package config files
#
# Consumers do `find_package(VulkanWindow CONFIG REQUIRED)` and link
# `VulkanWindow::VulkanWindow`. The exported config calls find_dependency for
# Vulkan, glfw3, and VulkanMemoryAllocator — those must be findable on the
# consumer's CMAKE_PREFIX_PATH (typically supplied by vcpkg/Conan or system
# packages). GLFW and VMA are NOT installed by us, since they're third-party
# submodules with their own versioning.
if(VULKANWINDOW_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set(VULKANWINDOW_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/VulkanWindow")

  install(TARGETS VulkanWindow
    EXPORT VulkanWindowTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/VulkanWindow)

  # imgui is a separate target (it has its own headers) but lives in the same
  # export set so a single find_package call brings both in.
  install(TARGETS imgui
    EXPORT VulkanWindowTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

  # Public headers — only the .h files from source/. .cpp files don't ship.
  install(DIRECTORY source/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/VulkanWindow
    FILES_MATCHING PATTERN "*.h")

  # Dear ImGui headers (matching the INSTALL_INTERFACE paths above).
  install(DIRECTORY deps/ImGui/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/VulkanWindow/imgui
    FILES_MATCHING
      PATTERN "*.h"
      PATTERN "backends/*.h")

  # Export the targets into a file the consumer will include.
  install(EXPORT VulkanWindowTargets
    FILE VulkanWindowTargets.cmake
    NAMESPACE VulkanWindow::
    DESTINATION ${VULKANWINDOW_CMAKE_DIR})

  # Generate the version + config files from templates in cmake/.
  configure_package_config_file(
    cmake/VulkanWindowConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/VulkanWindowConfig.cmake"
    INSTALL_DESTINATION ${VULKANWINDOW_CMAKE_DIR})

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VulkanWindowConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VulkanWindowConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VulkanWindowConfigVersion.cmake"
    # The shader helper, included by VulkanWindowConfig.cmake, so
    # vulkanwindow_add_shaders() is available to installed-package consumers too.
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanWindowShaders.cmake"
    DESTINATION ${VULKANWINDOW_CMAKE_DIR})
endif()
