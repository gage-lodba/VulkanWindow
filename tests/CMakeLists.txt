# Unit tests for the pure (no Vulkan instance / device / window) logic in the
# library. doctest is fetched only here, so it never touches a consumer build.
include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  # doctest is header-only; its bundled CMakeLists declares an ancient
  # cmake_minimum_required that modern CMake rejects. Point SOURCE_SUBDIR at a
  # path with no CMakeLists.txt so MakeAvailable downloads the source but skips
  # add_subdirectory — we wrap the header in our own interface target below.
  SOURCE_SUBDIR do-not-configure)
FetchContent_MakeAvailable(doctest)

add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
# The header is at <doctest/doctest.h> relative to the repo root.
target_include_directories(doctest INTERFACE ${doctest_SOURCE_DIR})

add_executable(VulkanWindowTests
  test_main.cpp
  test_utils.cpp
  test_selectors.cpp)

# Link the library to reach its headers + the functions under test. The tests
# only call pure functions, but the static lib's transitive deps (Vulkan, GLFW,
# VMA, imgui) link in harmlessly and need no runtime ICD/display to run.
target_link_libraries(VulkanWindowTests PRIVATE
  VulkanWindow::VulkanWindow
  doctest::doctest)

add_test(NAME VulkanWindowUnitTests COMMAND VulkanWindowTests)
