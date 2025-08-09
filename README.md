<div align="center">

# VulkanWindow

![Build Status](https://github.com/gage-lodba//VulkanWindow/actions/workflows/build.yml/badge.svg)

</div>

A minimalistic project designed for straightforward window creation using ImGui, GLFW, and Vulkan. It provides a hassle-free environment for developing graphical user interfaces with ease.

## Building from source

### Dependencies

[CMake](https://cmake.org/) is an open-source, cross-platform family of tools designed to build, test and package software.

[Vulkan SDK](https://www.vulkan.org/) is a cross-platform industry standard enabling developers to target a wide range of devices with the same graphics API.

### Windows

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build --config Release
```

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build --config Release
```

### Credits

[Roboto](https://github.com/googlefonts/roboto) font by Christian Robertson (Apache-2.0 license).  
