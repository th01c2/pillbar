#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "app/app.hpp"
#include "app/logging.hpp"

namespace {

void print_usage(const char* argv0) {
  std::printf(
      "pillbar - floating pill-shaped Wayland status bar for Hyprland\n"
      "\n"
      "Usage: %s [--help] [--version]\n"
      "\n"
      "Environment:\n"
      "  PILLBAR_LOG=error|warn|info|debug   log verbosity (default warn)\n"
      "  HYPRLAND_INSTANCE_SIGNATURE         required for the Hyprland source\n",
      argv0);
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    }
    if (arg == "--version") {
      std::printf("pillbar 0.1.0\n");
      return 0;
    }
    std::fprintf(stderr, "pillbar: unknown argument '%s'\n", arg.c_str());
    print_usage(argv[0]);
    return 2;
  }

  try {
    pillbar::App app;
    return app.run();
  } catch (const std::exception& error) {
    LOG_ERR("fatal: %s", error.what());
    return 1;
  }
}

