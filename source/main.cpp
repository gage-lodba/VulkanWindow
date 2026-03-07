#include <iostream>

#include "Application.h"
#include "UserInterface.h"

int main() {
  try {
    Application app;

    // Set up the default demo UI
    UserInterface ui;
    app.setUICallback([&ui]() { ui.render(); });

    app.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
