from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one match in {path}, found {count}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


# Resolve a modern Vulkan header set independently from the platform loader.
replace_once(
    ROOT / "CMakeLists.txt",
    "find_package(Vulkan REQUIRED)\ninclude(FetchContent)\ninclude(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/shared_dependencies.cmake\")\n",
    "include(FetchContent)\n"
    "include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/resolve_vulkan_dependency.cmake\")\n"
    "resolve_algoforge_vulkan_dependency()\n"
    "include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/shared_dependencies.cmake\")\n",
)
replace_once(
    ROOT / "CMakeLists.txt",
    "resolve_assimp_dependency(\"${CMAKE_CURRENT_SOURCE_DIR}\")\n\n",
    "resolve_assimp_dependency(\"${CMAKE_CURRENT_SOURCE_DIR}\")\n\n"
    "FetchContent_Declare(asio_source\n"
    "  URL \"https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.zip\"\n"
    ")\n"
    "FetchContent_GetProperties(asio_source)\n"
    "if(NOT asio_source_POPULATED)\n"
    "  FetchContent_Populate(asio_source)\n"
    "endif()\n"
    "set(ASIO_INCLUDE_DIR \"${asio_source_SOURCE_DIR}/asio/include\")\n"
    "find_package(Threads REQUIRED)\n\n",
)
replace_once(
    ROOT / "CMakeLists.txt",
    "target_compile_definitions(common_data PRIVATE PROJECT_ROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")\n",
    "target_compile_definitions(common_data\n"
    "  PUBLIC ALGOFORGE_PROJECT_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
    "  PRIVATE PROJECT_ROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
    ")\n",
)
replace_once(
    ROOT / "CMakeLists.txt",
    "add_executable(debugTool WIN32\n  src/debug_tool/main.cpp\n)\n\ntarget_link_libraries(debugTool PRIVATE\n  debug_tool_frontend\n  debug_tool_backend\n  common_data\n)\n\ntarget_compile_definitions(debugTool PRIVATE\n  WIN32_LEAN_AND_MEAN\n  NOMINMAX\n)\n",
    "add_executable(debugTool WIN32\n  src/debug_tool/main.cpp\n)\n\n"
    "target_include_directories(debugTool PRIVATE \"${ASIO_INCLUDE_DIR}\")\n\n"
    "target_link_libraries(debugTool PRIVATE\n"
    "  debug_tool_frontend\n"
    "  debug_tool_backend\n"
    "  common_data\n"
    "  Threads::Threads\n"
    ")\n\n"
    "target_compile_definitions(debugTool PRIVATE\n"
    "  ASIO_STANDALONE\n"
    "  ASIO_NO_DEPRECATED\n"
    ")\n",
)
replace_once(
    ROOT / "CMakeLists.txt",
    "target_link_libraries(debugTool PRIVATE advapi32 user32 gdi32 shell32)\n",
    "target_link_libraries(debugTool PRIVATE advapi32 user32 gdi32 shell32 ws2_32)\n",
)

algorithm_cmake = ROOT / "algorithmLib/CMakeLists.txt"
replace_once(
    algorithm_cmake,
    "find_package(Vulkan REQUIRED)\nfind_program(GLSLC glslc REQUIRED)\ninclude(FetchContent)\ninclude(\"${REPO_ROOT}/cmake/shared_dependencies.cmake\")\n",
    "include(FetchContent)\n"
    "include(\"${REPO_ROOT}/cmake/resolve_vulkan_dependency.cmake\")\n"
    "resolve_algoforge_vulkan_dependency()\n"
    "find_program(GLSLC glslc REQUIRED)\n"
    "include(\"${REPO_ROOT}/cmake/shared_dependencies.cmake\")\n",
)
replace_once(
    algorithm_cmake,
    "  target_include_directories(algorithm_plugin_support_mirror PUBLIC\n"
    "    \"${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}\"\n"
    "    \"${CJSON_SOURCE_DIR}\"\n"
    "  )\n",
    "  target_include_directories(algorithm_plugin_support_mirror PUBLIC\n"
    "    \"${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}\"\n"
    "    \"${CJSON_SOURCE_DIR}\"\n"
    "  )\n"
    "  target_compile_definitions(algorithm_plugin_support_mirror PUBLIC\n"
    "    ALGOFORGE_PROJECT_ROOT=\"${REPO_ROOT}\"\n"
    "  )\n",
)

# The project root is a build-system input. Runtime code does not query the OS
# for an executable path.
paths_header = ROOT / "src/algomanager/bridge/algorithm_library_paths.h"
replace_once(
    paths_header,
    "#include <array>\n#include <filesystem>\n#include <initializer_list>\n#include <string>\n\n"
    "#if defined(_WIN32)\n"
    "#ifndef NOMINMAX\n"
    "#define NOMINMAX\n"
    "#endif\n"
    "#include <windows.h>\n"
    "#elif defined(__linux__)\n"
    "#include <limits.h>\n"
    "#include <unistd.h>\n"
    "#elif defined(__APPLE__)\n"
    "#include <mach-o/dyld.h>\n"
    "#include <limits.h>\n"
    "#endif\n",
    "#include <array>\n#include <filesystem>\n#include <initializer_list>\n#include <string>\n\n"
    "#ifndef ALGOFORGE_PROJECT_ROOT\n"
    "#define ALGOFORGE_PROJECT_ROOT \"\"\n"
    "#endif\n",
)
replace_once(
    paths_header,
    "inline fs::path ResolveExecutableDirectory() {\n"
    "#if defined(_WIN32)\n"
    "  std::array<wchar_t, 32768> buffer{};\n"
    "  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));\n"
    "  return fs::path(std::wstring(buffer.data(), length)).parent_path();\n"
    "#elif defined(__linux__)\n"
    "  std::array<char, PATH_MAX> buffer{};\n"
    "  const ssize_t length = readlink(\"/proc/self/exe\", buffer.data(), buffer.size());\n"
    "  return fs::path(std::string(buffer.data(), static_cast<size_t>(length))).parent_path();\n"
    "#elif defined(__APPLE__)\n"
    "  std::array<char, PATH_MAX> buffer{};\n"
    "  uint32_t size = static_cast<uint32_t>(buffer.size());\n"
    "  _NSGetExecutablePath(buffer.data(), &size);\n"
    "  return fs::path(buffer.data()).parent_path();\n"
    "#else\n"
    "  return {};\n"
    "#endif\n"
    "}\n\n"
    "inline fs::path ResolveExecutableProjectRoot() {\n"
    "  const fs::path executable_directory = ResolveExecutableDirectory();\n"
    "  if (executable_directory.empty()) {\n"
    "    return {};\n"
    "  }\n\n"
    "  const std::array<fs::path, 3> runtime_root_candidates = {\n"
    "    executable_directory / \"../../algorithmLib/algorithmruntimeLib\",\n"
    "    executable_directory / \"../../../algorithmLib/algorithmruntimeLib\",\n"
    "    executable_directory / \"../../../../algorithmLib/algorithmruntimeLib\"};\n"
    "  for (const fs::path& runtime_root : runtime_root_candidates) {\n"
    "    std::error_code ec;\n"
    "    if (!fs::is_directory(runtime_root, ec)) {\n"
    "      continue;\n"
    "    }\n"
    "    const fs::path project_root = runtime_root.parent_path().parent_path();\n"
    "    if (fs::is_directory(project_root / \"algorithmLib/algorithmSrc\", ec)) {\n"
    "      return project_root;\n"
    "    }\n"
    "  }\n"
    "  return {};\n"
    "}\n",
    "inline fs::path ResolveExecutableDirectory() {\n"
    "  std::error_code ec;\n"
    "  return fs::current_path(ec);\n"
    "}\n\n"
    "inline fs::path ResolveExecutableProjectRoot() {\n"
    "  std::error_code ec;\n"
    "  const fs::path configured_root(ALGOFORGE_PROJECT_ROOT);\n"
    "  if (!configured_root.empty() &&\n"
    "      fs::is_directory(configured_root / \"algorithmLib/algorithmSrc\", ec)) {\n"
    "    return configured_root;\n"
    "  }\n\n"
    "  fs::path candidate = ResolveExecutableDirectory();\n"
    "  for (size_t depth = 0u; depth < 6u && !candidate.empty(); ++depth) {\n"
    "    ec.clear();\n"
    "    if (fs::is_directory(candidate / \"algorithmLib/algorithmSrc\", ec)) {\n"
    "      return candidate;\n"
    "    }\n"
    "    candidate = candidate.parent_path();\n"
    "  }\n"
    "  return {};\n"
    "}\n",
)

# Crash dump collection was a Win32-only concern embedded in the main entry.
# Remove it from the platform-neutral application layer.
main_cpp = ROOT / "src/debug_tool/main.cpp"
replace_once(
    main_cpp,
    "#ifdef _WIN32\n#define WIN32_LEAN_AND_MEAN\n#include <windows.h>\n#include <dbghelp.h>\n#pragma comment(lib, \"Dbghelp.lib\")\n#endif\n\n",
    "",
)
text = main_cpp.read_text(encoding="utf-8")
block_begin = text.index("#ifdef _WIN32\nLONG WINAPI _WriteCrashDump")
block_end_marker = "#endif\n\nconst char* _AssemblyStateName"
block_end = text.index(block_end_marker, block_begin)
text = text[:block_begin] + "const char* _AssemblyStateName" + text[block_end + len(block_end_marker):]
text = text.replace(
    "#ifdef _WIN32\n    _InstallCrashDumpHandler();\n#endif\n",
    "",
    1,
)
main_cpp.write_text(text, encoding="utf-8")

# Use standalone Asio for the local runner protocol. Platform socket headers,
# handle types, startup and cleanup are contained by the dependency.
runner_header = ROOT / "src/debug_tool/runner_control_socket.h"
runner_header.write_text(r'''#pragma once

#include <asio.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace debug_tool_backend::runner_control {

struct Endpoint {
  std::string host{"127.0.0.1"};
  uint16_t port{0u};
};

struct BinaryResponse {
  std::string header{};
  std::vector<std::byte> body{};
};

inline bool ParseEndpoint(const std::string& text, Endpoint* out_endpoint) {
  if (!out_endpoint) {
    return false;
  }
  const size_t colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0u || colon + 1u >= text.size()) {
    return false;
  }

  Endpoint endpoint{};
  endpoint.host = text.substr(0u, colon);
  if (endpoint.host == "localhost") {
    endpoint.host = "127.0.0.1";
  }

  char* end = nullptr;
  const unsigned long parsed_port = std::strtoul(text.c_str() + colon + 1u, &end, 10);
  if (!end || *end != '\0' || parsed_port > 65535ul) {
    return false;
  }

  endpoint.port = static_cast<uint16_t>(parsed_port);
  *out_endpoint = std::move(endpoint);
  return true;
}

inline std::string FormatEndpoint(const Endpoint& endpoint) {
  return endpoint.host + ":" + std::to_string(endpoint.port);
}

inline std::string QuoteToken(const std::string& token) {
  if (token.find_first_of(" \t\"") == std::string::npos) {
    return token;
  }
  std::string quoted{"\""};
  for (char ch : token) {
    if (ch == '"') {
      quoted.push_back('\\');
    }
    quoted.push_back(ch);
  }
  quoted.push_back('"');
  return quoted;
}

inline std::string JoinCommandLine(int argc, char** argv, int begin_index) {
  std::ostringstream stream;
  for (int i = begin_index; i < argc; ++i) {
    if (i > begin_index) {
      stream << ' ';
    }
    stream << QuoteToken(argv[i] ? std::string(argv[i]) : std::string{});
  }
  return stream.str();
}

inline std::vector<std::string> TokenizeCommandLine(const std::string& command_line) {
  std::vector<std::string> tokens{};
  std::string current{};
  bool in_quotes = false;
  for (size_t i = 0u; i < command_line.size(); ++i) {
    const char ch = command_line[i];
    if (ch == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
      if (!current.empty()) {
        tokens.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    if (ch == '\\' && in_quotes && i + 1u < command_line.size() && command_line[i + 1u] == '"') {
      current.push_back('"');
      ++i;
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(std::move(current));
  }
  return tokens;
}

namespace detail {

using Tcp = asio::ip::tcp;

inline asio::ip::address Address(const Endpoint& endpoint) {
  return asio::ip::make_address(endpoint.host.empty() ? "127.0.0.1" : endpoint.host);
}

inline bool WriteAll(Tcp::socket& socket, const std::string& payload) {
  asio::error_code ec;
  asio::write(socket, asio::buffer(payload.data(), payload.size()), ec);
  return !ec;
}

inline bool WriteAll(Tcp::socket& socket, const std::byte* payload, size_t payload_size) {
  asio::error_code ec;
  asio::write(socket, asio::buffer(payload, payload_size), ec);
  return !ec;
}

inline bool ReadLine(Tcp::socket& socket, std::string* out_line) {
  if (!out_line) {
    return false;
  }
  asio::streambuf buffer;
  asio::error_code ec;
  asio::read_until(socket, buffer, '\n', ec);
  if (ec && ec != asio::error::eof) {
    return false;
  }
  std::istream stream(&buffer);
  std::getline(stream, *out_line);
  if (!out_line->empty() && out_line->back() == '\r') {
    out_line->pop_back();
  }
  return !out_line->empty();
}

inline void SetError(std::string* out_error_message, const std::string& message) {
  if (out_error_message) {
    *out_error_message = message;
  }
}

}  // namespace detail

inline bool SendCommand(
  const Endpoint& endpoint,
  const std::string& request,
  std::string* out_response,
  std::string* out_error_message) {
  try {
    if (out_response) {
      out_response->clear();
    }
    asio::io_context io_context;
    detail::Tcp::socket socket(io_context);
    socket.connect(detail::Tcp::endpoint(detail::Address(endpoint), endpoint.port));

    std::string payload = request;
    payload.push_back('\n');
    if (!detail::WriteAll(socket, payload)) {
      detail::SetError(out_error_message, "Failed to send runner control request.");
      return false;
    }

    asio::error_code shutdown_error;
    socket.shutdown(detail::Tcp::socket::shutdown_send, shutdown_error);

    std::string response;
    std::array<char, 4096> buffer{};
    for (;;) {
      asio::error_code read_error;
      const size_t received = socket.read_some(asio::buffer(buffer), read_error);
      if (received > 0u) {
        response.append(buffer.data(), received);
      }
      if (read_error == asio::error::eof) {
        break;
      }
      if (read_error) {
        detail::SetError(out_error_message, "Failed to read runner control response: " + read_error.message());
        return false;
      }
    }

    if (out_response) {
      *out_response = std::move(response);
    }
    detail::SetError(out_error_message, {});
    return true;
  } catch (const std::exception& error) {
    detail::SetError(out_error_message, "Runner control client failed: " + std::string(error.what()));
    return false;
  }
}

inline bool RunServer(
  const Endpoint& endpoint,
  const std::function<std::string(const std::string&, bool* out_shutdown_requested)>& handler,
  const std::function<void(const Endpoint&)>& on_bound_endpoint = {},
  std::string* out_error_message = nullptr) {
  try {
    asio::io_context io_context;
    detail::Tcp::acceptor acceptor(io_context);
    const detail::Tcp::endpoint listen_endpoint(detail::Address(endpoint), endpoint.port);
    acceptor.open(listen_endpoint.protocol());
    acceptor.set_option(detail::Tcp::acceptor::reuse_address(true));
    acceptor.bind(listen_endpoint);
    acceptor.listen();

    Endpoint bound_endpoint = endpoint;
    bound_endpoint.port = acceptor.local_endpoint().port();
    if (on_bound_endpoint) {
      on_bound_endpoint(bound_endpoint);
    }

    bool should_shutdown = false;
    while (!should_shutdown) {
      detail::Tcp::socket client_socket(io_context);
      acceptor.accept(client_socket);

      std::string request;
      if (!detail::ReadLine(client_socket, &request)) {
        (void)detail::WriteAll(client_socket, "ERR empty request\n");
        continue;
      }

      bool request_shutdown = false;
      std::string response = handler(request, &request_shutdown);
      if (response.empty()) {
        response = "OK\n";
      } else if (response.back() != '\n') {
        response.push_back('\n');
      }
      (void)detail::WriteAll(client_socket, response);
      should_shutdown = request_shutdown;
    }

    detail::SetError(out_error_message, {});
    return true;
  } catch (const std::exception& error) {
    detail::SetError(out_error_message, "Runner control server failed: " + std::string(error.what()));
    return false;
  }
}

inline bool RunBinaryServer(
  const Endpoint& endpoint,
  const std::function<BinaryResponse(const std::string&, bool* out_shutdown_requested)>& handler,
  const std::function<void(const Endpoint&)>& on_bound_endpoint = {},
  std::string* out_error_message = nullptr) {
  try {
    asio::io_context io_context;
    detail::Tcp::acceptor acceptor(io_context);
    const detail::Tcp::endpoint listen_endpoint(detail::Address(endpoint), endpoint.port);
    acceptor.open(listen_endpoint.protocol());
    acceptor.set_option(detail::Tcp::acceptor::reuse_address(true));
    acceptor.bind(listen_endpoint);
    acceptor.listen();

    Endpoint bound_endpoint = endpoint;
    bound_endpoint.port = acceptor.local_endpoint().port();
    if (on_bound_endpoint) {
      on_bound_endpoint(bound_endpoint);
    }

    bool should_shutdown = false;
    while (!should_shutdown) {
      detail::Tcp::socket client_socket(io_context);
      acceptor.accept(client_socket);

      std::string request;
      if (!detail::ReadLine(client_socket, &request)) {
        (void)detail::WriteAll(client_socket, "ERR empty request\n");
        continue;
      }

      bool request_shutdown = false;
      BinaryResponse response = handler(request, &request_shutdown);
      if (response.header.empty()) {
        response.header = "OK";
      }
      if (response.header.back() != '\n') {
        response.header.push_back('\n');
      }
      (void)detail::WriteAll(client_socket, response.header);
      if (!response.body.empty()) {
        (void)detail::WriteAll(client_socket, response.body.data(), response.body.size());
      }
      should_shutdown = request_shutdown;
    }

    detail::SetError(out_error_message, {});
    return true;
  } catch (const std::exception& error) {
    detail::SetError(out_error_message, "Runner control binary server failed: " + std::string(error.what()));
    return false;
  }
}

}  // namespace debug_tool_backend::runner_control
''', encoding="utf-8")
