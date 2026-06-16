#include <iostream>

#include "Application.h"
#include "UserInterface.h"

auto main() -> int {
  try {
    Application app;

    // Set up the default demo UI
    UserInterface ui;
    app.setUICallback([&ui]() -> void { ui.render(); });

    app.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
