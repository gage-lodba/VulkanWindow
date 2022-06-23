#include "window.h"
#include <iostream>

int main() {
	Window window;

	try {
		window.run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}