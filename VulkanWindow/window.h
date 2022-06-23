#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <glfw3.h>

#include <vulkan/vulkan.h>
#include <cstdlib>

class Window {
public:
	void run() {
		initializeVulkan();
		mainLoop();
		cleanup();
	}

private:
	const uint32_t WIDTH = 800;
	const uint32_t HEIGHT = 600;

	GLFWwindow* window;

	void initializeVulkan() {
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);

		glfwMakeContextCurrent(window);
	}

	void mainLoop() {
		while (!glfwWindowShouldClose(window)) {
			glfwSwapBuffers(window);
			glfwPollEvents();
		}
	}

	void cleanup() {
		glfwDestroyWindow(window);
		glfwTerminate();
	}
};