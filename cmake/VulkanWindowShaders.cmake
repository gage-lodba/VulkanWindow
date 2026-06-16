# VulkanWindowShaders.cmake
#
# Provides vulkanwindow_add_shaders() — compile GLSL to SPIR-V with glslc and
# embed it as a C initializer list that C++ can #include, so a binary carries
# its SPIR-V with no runtime file dependency. Available after
# add_subdirectory()/FetchContent of VulkanWindow and after
# find_package(VulkanWindow).

include_guard(GLOBAL)

# Locate glslc once. Honour an explicit VULKANWINDOW_GLSLC, then CMake
# FindVulkan's result (>= 3.19), then PATH / the Vulkan SDK bin dir.
if(NOT VULKANWINDOW_GLSLC)
  if(Vulkan_GLSLC_EXECUTABLE)
    set(VULKANWINDOW_GLSLC "${Vulkan_GLSLC_EXECUTABLE}"
      CACHE FILEPATH "glslc SPIR-V compiler (from the Vulkan SDK)")
  else()
    find_program(VULKANWINDOW_GLSLC
      NAMES glslc
      HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin"
      DOC "glslc SPIR-V compiler (from the Vulkan SDK)")
  endif()
endif()

# Cache (global) so the flag is visible in a consumer's directory scope, not
# just inside VulkanWindow's own subdirectory where this module is included.
if(VULKANWINDOW_GLSLC)
  set(VULKANWINDOW_GLSLC_FOUND TRUE CACHE INTERNAL "VulkanWindow: glslc available")
else()
  set(VULKANWINDOW_GLSLC_FOUND FALSE CACHE INTERNAL "VulkanWindow: glslc available")
endif()

# vulkanwindow_add_shaders(<target> <shader>... [OUTPUT_DIR <dir>])
#
# For each GLSL <shader> (stage inferred from its extension by glslc), compile
# it into "<filename>.spv.inc" — a brace-enclosed list of 32-bit SPIR-V words —
# under OUTPUT_DIR (default: a per-target dir in the build tree). Each generated
# file is added to <target> as a build dependency and OUTPUT_DIR is put on the
# target's private include path, so the source can write:
#
#     constexpr uint32_t kSpv[] =
#     #include "shader.vert.spv.inc"
#         ;
#
# Requires glslc; errors if it wasn't found. Guard the call with
# if(VULKANWINDOW_GLSLC_FOUND) when a missing compiler should skip rather than
# fail (e.g. an optional example).
function(vulkanwindow_add_shaders target)
  # Key off the cache variable directly — it's global, so this is correct no
  # matter which directory scope the function is called from.
  if(NOT VULKANWINDOW_GLSLC)
    message(FATAL_ERROR
      "vulkanwindow_add_shaders(${target}): glslc not found. Install the "
      "Vulkan SDK (or set -DVULKANWINDOW_GLSLC=/path/to/glslc), or guard the "
      "call with if(VULKANWINDOW_GLSLC_FOUND).")
  endif()

  cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_DIR" "")
  set(shaders ${arg_UNPARSED_ARGUMENTS})
  if(NOT shaders)
    message(FATAL_ERROR
      "vulkanwindow_add_shaders(${target}): no shader files given.")
  endif()

  set(out_dir "${arg_OUTPUT_DIR}")
  if(NOT out_dir)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}_shaders")
  endif()

  set(generated "")
  foreach(shader IN LISTS shaders)
    get_filename_component(shader_abs "${shader}" ABSOLUTE)
    get_filename_component(shader_name "${shader}" NAME)
    set(out "${out_dir}/${shader_name}.spv.inc")
    add_custom_command(
      OUTPUT "${out}"
      # Ninja pre-creates output dirs; Unix Makefiles / VS generators do not, so
      # make the dir ourselves or glslc -o fails with "cannot open output file".
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
      COMMAND "${VULKANWINDOW_GLSLC}" -mfmt=c -o "${out}" "${shader_abs}"
      DEPENDS "${shader_abs}"
      COMMENT "glslc: ${shader_name} -> SPIR-V (embedded)"
      VERBATIM)
    list(APPEND generated "${out}")
  endforeach()

  target_sources(${target} PRIVATE ${generated})
  target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
