#pragma once

#include <asio.hpp>

#include <algorithm>
#include <array>
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
