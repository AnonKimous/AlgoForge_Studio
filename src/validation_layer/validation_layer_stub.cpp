#include "validation_layer.h"

#include <algorithm>
#include <cctype>

ValidationLayerApi::~ValidationLayerApi() {
  Stop();
}

bool ValidationLayerApi::Start() {
  enabled_.store(false);
  return false;
}

void ValidationLayerApi::Stop() {
  stop_requested_.store(false);
  enabled_.store(false);
}

void ValidationLayerApi::PublishFrame(const ValidationFrameSnapshot&) {
}

bool ValidationLayerApi::ParseStartupFlag(const std::vector<std::string>& args) {
  bool enabled = false;
  for (size_t i = 1; i < args.size(); ++i) {
    std::string arg = args[i];
    while (!arg.empty() && (arg.front() == '-' || arg.front() == '/')) {
      arg.erase(arg.begin());
    }
    std::transform(arg.begin(), arg.end(), arg.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (arg == "validationlayer:on" || arg == "vaildlayer:on") {
      enabled = true;
    } else if (arg == "validationlayer:off" || arg == "vaildlayer:off") {
      enabled = false;
    }
  }
  return enabled;
}
