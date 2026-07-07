#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace debug_tool_backend::runner_control {

struct Endpoint {
  std::string host{"127.0.0.1"};
  uint16_t port{0u};
};

inline bool ParseEndpoint(const std::string& text, Endpoint* out_endpoint) {
  if (!out_endpoint) {
    return false;
  }
  const std::string trimmed = text;
  const size_t colon = trimmed.rfind(':');
  if (colon == std::string::npos || colon == 0u || colon + 1u >= trimmed.size()) {
    return false;
  }

  Endpoint endpoint{};
  endpoint.host = trimmed.substr(0u, colon);
  if (endpoint.host == "localhost") {
    endpoint.host = "127.0.0.1";
  }

  char* end = nullptr;
  const unsigned long parsed_port = std::strtoul(trimmed.c_str() + colon + 1u, &end, 10);
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

inline bool _WriteAll(SOCKET socket, const std::string& payload) {
  size_t offset = 0u;
  while (offset < payload.size()) {
    const int written = send(
      socket,
      payload.data() + offset,
      static_cast<int>(payload.size() - offset),
      0);
    if (written == SOCKET_ERROR) {
      return false;
    }
    offset += static_cast<size_t>(written);
  }
  return true;
}

inline bool _ReadLine(SOCKET socket, std::string* out_line) {
  if (!out_line) {
    return false;
  }
  out_line->clear();
  char buffer[4096]{};
  while (true) {
    const int received = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
    if (received == SOCKET_ERROR || received == 0) {
      return !out_line->empty();
    }
    for (int i = 0; i < received; ++i) {
      const char ch = buffer[i];
      if (ch == '\n') {
        return true;
      }
      if (ch != '\r') {
        out_line->push_back(ch);
      }
    }
  }
}

inline bool SendCommand(
  const Endpoint& endpoint,
  const std::string& request,
  std::string* out_response,
  std::string* out_error_message) {
#ifndef _WIN32
  (void)endpoint;
  (void)request;
  (void)out_response;
  if (out_error_message) {
    *out_error_message = "Runner control is only supported on Windows.";
  }
  return false;
#else
  if (out_response) {
    out_response->clear();
  }

  WSADATA wsa_data{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    if (out_error_message) {
      *out_error_message = "WSAStartup failed.";
    }
    return false;
  }

  SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_handle == INVALID_SOCKET) {
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to create runner control socket.";
    }
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (endpoint.host == "127.0.0.1" || endpoint.host.empty()) {
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    closesocket(socket_handle);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to parse runner control endpoint host '" + endpoint.host + "'.";
    }
    return false;
  }

  if (connect(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
    closesocket(socket_handle);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to connect to runner control endpoint " + FormatEndpoint(endpoint) + ".";
    }
    return false;
  }

  std::string payload = request;
  payload.push_back('\n');
  if (!_WriteAll(socket_handle, payload)) {
    closesocket(socket_handle);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to send runner control request.";
    }
    return false;
  }

  shutdown(socket_handle, SD_SEND);

  std::string response{};
  char buffer[4096]{};
  while (true) {
    const int received = recv(socket_handle, buffer, static_cast<int>(sizeof(buffer)), 0);
    if (received == SOCKET_ERROR) {
      closesocket(socket_handle);
      WSACleanup();
      if (out_error_message) {
        *out_error_message = "Failed to read runner control response.";
      }
      return false;
    }
    if (received == 0) {
      break;
    }
    response.append(buffer, buffer + received);
  }

  closesocket(socket_handle);
  WSACleanup();
  if (out_response) {
    *out_response = std::move(response);
  }
  return true;
#endif
}

inline bool RunServer(
  const Endpoint& endpoint,
  const std::function<std::string(const std::string&, bool* out_shutdown_requested)>& handler,
  const std::function<void(const Endpoint&)>& on_bound_endpoint = {},
  std::string* out_error_message = nullptr) {
#ifndef _WIN32
  (void)endpoint;
  (void)handler;
  if (out_error_message) {
    *out_error_message = "Runner control is only supported on Windows.";
  }
  return false;
#else
  WSADATA wsa_data{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    if (out_error_message) {
      *out_error_message = "WSAStartup failed.";
    }
    return false;
  }

  SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket == INVALID_SOCKET) {
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to create runner control server socket.";
    }
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (endpoint.host == "127.0.0.1" || endpoint.host.empty()) {
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    closesocket(listen_socket);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to parse runner control server host '" + endpoint.host + "'.";
    }
    return false;
  }

  if (bind(listen_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
    closesocket(listen_socket);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to bind runner control server to " + FormatEndpoint(endpoint) + ".";
    }
    return false;
  }

  if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
    closesocket(listen_socket);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to listen on runner control server socket.";
    }
    return false;
  }

  Endpoint bound_endpoint = endpoint;
  sockaddr_in bound_address{};
  int bound_address_size = sizeof(bound_address);
  if (getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound_address), &bound_address_size) == SOCKET_ERROR) {
    closesocket(listen_socket);
    WSACleanup();
    if (out_error_message) {
      *out_error_message = "Failed to resolve runner control server port.";
    }
    return false;
  }
  bound_endpoint.port = ntohs(bound_address.sin_port);
  if (on_bound_endpoint) {
    on_bound_endpoint(bound_endpoint);
  }

  bool should_shutdown = false;
  while (!should_shutdown) {
    SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
    if (client_socket == INVALID_SOCKET) {
      closesocket(listen_socket);
      WSACleanup();
      if (out_error_message) {
        *out_error_message = "Failed to accept runner control client connection.";
      }
      return false;
    }

    std::string request{};
    if (!_ReadLine(client_socket, &request)) {
      const std::string response = "ERR empty request\n";
      (void)_WriteAll(client_socket, response);
      closesocket(client_socket);
      continue;
    }

    bool request_shutdown = false;
    std::string response = handler(request, &request_shutdown);
    if (response.empty()) {
      response = "OK\n";
    } else if (response.back() != '\n') {
      response.push_back('\n');
    }
    (void)_WriteAll(client_socket, response);
    closesocket(client_socket);

    if (request_shutdown) {
      should_shutdown = true;
    }
  }

  closesocket(listen_socket);
  WSACleanup();
  return true;
#endif
}

}  // namespace debug_tool_backend::runner_control
