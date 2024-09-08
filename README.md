### VulkanWindow
A minimalistic project designed for straightforward window creation using ImGui, GLFW, and Vulkan. It provides a hassle-free environment for developing graphical user interfaces with ease.

### Tested platforms
Tested to work on both windows and linux.

### Dependencies:
[msys2](https://www.msys2.org/) is a collection of tools and libraries providing you with an easy-to-use environment for building, installing and running native Windows software. (Windows requirement)

[CMake](https://cmake.org/) is an open-source, cross-platform family of tools designed to build, test and package software.

[Dear ImGui](https://github.com/ocornut/imgui) is a bloat-free graphical user interface library for C++.

[GLFW](https://github.com/glfw/glfw) is an Open Source, multi-platform library for OpenGL, OpenGL ES and Vulkan application development.

[Vulkan](https://www.vulkan.org/) is a cross-platform industry standard enabling developers to target a wide range of devices with the same graphics API. 

### Building from source

#### Windows
```
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc

cmake --build build --config Release
```
#### Linux
```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc

cmake --build build --config Release
```

### Credits
[Roboto](https://github.com/googlefonts/roboto) font by Christian Robertson (Apache-2.0 license).