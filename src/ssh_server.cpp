/**
 * @file src/ssh_server.cpp
 * @brief SSH server implementation for USB/IP port forwarding
 */

#include "ssh_server.h"
#include "logging.h"

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#ifdef _WIN32
  #pragma comment(lib, "ws2_32.lib")
#endif

namespace ssh_server {

  // Global server instance
  static std::shared_ptr<ssh_server_t> global_server;

  class ssh_server_t::impl {
  public:
    ssh_bind bind = nullptr;
    ssh_session session = nullptr;

    impl() {
      bind = ssh_bind_new();
    }

    ~impl() {
      if (bind) {
        ssh_bind_free(bind);
        bind = nullptr;
      }
    }
  };

  ssh_server_t::ssh_server_t():
      pimpl(std::make_unique<impl>()),
      running_(false),
      listen_port_(0) {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
  }

  ssh_server_t::~ssh_server_t() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
  }

  // Generate random password
  std::string
  generate_random_password(int length = 16) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    const int charset_size = sizeof(charset) - 1;
    std::string password;
    password.reserve(length);

    // Use C++11 random
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charset_size - 1);

    for (int i = 0; i < length; ++i) {
      password += charset[dis(gen)];
    }

    return password;
  }

  bool
  ssh_server_t::start(int port, const std::string &username, const std::string &password) {
    if (running_) {
      BOOST_LOG(warning) << "SSH server is already running";
      return false;
    }

    username_ = username;
    // Generate random password if not provided or empty
    if (password.empty()) {
      password_ = generate_random_password(20);
      BOOST_LOG(info) << "Generated random SSH password: " << password_;
    }
    else {
      password_ = password;
    }

    // Configure SSH bind
    if (port == 0) {
      // Dynamic port allocation - find an available port
      port = 2222;  // Start from 2222
      for (int try_port = port; try_port < port + 100; ++try_port) {
        ssh_bind_options_set(pimpl->bind, SSH_BIND_OPTIONS_BINDPORT, &try_port);
        if (ssh_bind_listen(pimpl->bind) == SSH_OK) {
          listen_port_ = try_port;
          break;
        }
      }
      if (listen_port_ == 0) {
        BOOST_LOG(error) << "Failed to bind SSH server to any port";
        return false;
      }
    }
    else {
      listen_port_ = port;
      ssh_bind_options_set(pimpl->bind, SSH_BIND_OPTIONS_BINDPORT, &port);
      if (ssh_bind_listen(pimpl->bind) != SSH_OK) {
        BOOST_LOG(error) << "Failed to bind SSH server to port " << port << ": " << ssh_get_error(pimpl->bind);
        return false;
      }
    }

    // Generate or load host keys
    // For now, generate RSA key on-the-fly (should be persisted in production)
    ssh_bind_options_set(pimpl->bind, SSH_BIND_OPTIONS_RSAKEY, nullptr);

    BOOST_LOG(info) << "SSH server listening on port " << listen_port_;

    running_ = true;
    server_thread_ = std::thread(&ssh_server_t::server_thread, this);

    return true;
  }

  void
  ssh_server_t::stop() {
    if (!running_) {
      return;
    }

    BOOST_LOG(info) << "Stopping SSH server...";
    running_ = false;

    if (server_thread_.joinable()) {
      server_thread_.join();
    }

    // Clean up all sessions
    {
      std::lock_guard<std::mutex> lock(tunnels_mutex_);
      tunnels_.clear();
    }

    BOOST_LOG(info) << "SSH server stopped";
  }

  bool
  ssh_server_t::is_running() const {
    return running_;
  }

  int
  ssh_server_t::get_port() const {
    return listen_port_;
  }

  std::string
  ssh_server_t::get_password() const {
    return password_;
  }

  std::vector<tunnel_info_t>
  ssh_server_t::get_active_tunnels() const {
    std::lock_guard<std::mutex> lock(tunnels_mutex_);
    std::vector<tunnel_info_t> result;
    for (const auto &[id, tunnel] : tunnels_) {
      if (tunnel && tunnel->active) {
        result.push_back(*tunnel);
      }
    }
    return result;
  }

  std::shared_ptr<tunnel_info_t>
  ssh_server_t::get_tunnel(const std::string &client_id) const {
    std::lock_guard<std::mutex> lock(tunnels_mutex_);
    auto it = tunnels_.find(client_id);
    if (it != tunnels_.end()) {
      return it->second;
    }
    return nullptr;
  }

  void
  ssh_server_t::set_tunnel_callback(std::function<void(const tunnel_info_t &)> callback) {
    tunnel_callback_ = callback;
  }

  void
  ssh_server_t::server_thread() {
    BOOST_LOG(info) << "SSH server thread started";

    while (running_) {
      // Accept incoming connections with timeout
      ssh_session session = ssh_new();
      if (!session) {
        BOOST_LOG(error) << "Failed to create SSH session";
        continue;
      }

      // Set a timeout for accept
      struct timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      int rc = ssh_bind_accept_fd(pimpl->bind, session, 0);
      if (rc == SSH_ERROR) {
        ssh_free(session);
        continue;
      }

      // Handle client in a separate thread
      std::thread([this, session]() {
        handle_client(session);
      }).detach();
    }

    BOOST_LOG(info) << "SSH server thread stopped";
  }

  void
  ssh_server_t::handle_client(void *session_ptr) {
    ssh_session session = static_cast<ssh_session>(session_ptr);

    // Exchange keys and authenticate
    if (ssh_handle_key_exchange(session) != SSH_OK) {
      BOOST_LOG(error) << "SSH key exchange failed: " << ssh_get_error(session);
      ssh_disconnect(session);
      ssh_free(session);
      return;
    }

    // Handle authentication
    ssh_message message;
    bool authenticated = false;
    std::string client_username;

    while ((message = ssh_message_get(session)) != nullptr && !authenticated) {
      int type = ssh_message_type(message);
      int subtype = ssh_message_subtype(message);

      if (type == SSH_REQUEST_AUTH && subtype == SSH_AUTH_METHOD_PASSWORD) {
        client_username = ssh_message_auth_user(message);
        const char *password = ssh_message_auth_password(message);

        if (client_username == username_ && std::string(password) == password_) {
          ssh_message_auth_reply_success(message, 0);
          authenticated = true;
          BOOST_LOG(info) << "SSH client authenticated: " << client_username;
        }
        else {
          ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_PASSWORD);
          ssh_message_reply_default(message);
        }
      }
      else {
        ssh_message_reply_default(message);
      }

      ssh_message_free(message);
    }

    if (!authenticated) {
      BOOST_LOG(warning) << "SSH client authentication failed";
      ssh_disconnect(session);
      ssh_free(session);
      return;
    }

    // Handle channel requests (port forwarding)
    ssh_channel channel;
    while ((message = ssh_message_get(session)) != nullptr) {
      int type = ssh_message_type(message);

      if (type == SSH_REQUEST_CHANNEL_OPEN) {
        channel = ssh_message_channel_request_open_reply_accept(message);
        ssh_message_free(message);

        // Handle channel requests
        while ((message = ssh_message_get(session)) != nullptr) {
          type = ssh_message_type(message);
          int subtype = ssh_message_subtype(message);

          if (type == SSH_REQUEST_CHANNEL && subtype == SSH_CHANNEL_REQUEST_SHELL) {
            // We don't provide shell access, only port forwarding
            ssh_message_reply_default(message);
          }
          else if (type == SSH_REQUEST_GLOBAL && subtype == SSH_GLOBAL_REQUEST_TCPIP_FORWARD) {
            // Handle reverse port forwarding request
            const char *bind_addr = ssh_message_global_request_address(message);
            int bind_port = ssh_message_global_request_port(message);

            BOOST_LOG(info) << "SSH client requests reverse port forward: " << bind_addr << ":" << bind_port;

            // Create tunnel info
            auto tunnel = std::make_shared<tunnel_info_t>();
            tunnel->client_id = client_username + "@" + std::string(ssh_get_disconnect_message(session));
            tunnel->client_ip = std::string(ssh_get_serverbanner(session));
            tunnel->forwarded_port = bind_port;
            tunnel->remote_usbip_port = bind_port;
            tunnel->active = true;

            {
              std::lock_guard<std::mutex> lock(tunnels_mutex_);
              tunnels_[tunnel->client_id] = tunnel;
            }

            // Notify callback
            if (tunnel_callback_) {
              tunnel_callback_(*tunnel);
            }

            ssh_message_global_request_reply_success(message, bind_port);
          }
          else {
            ssh_message_reply_default(message);
          }

          ssh_message_free(message);
        }
        break;
      }
      else {
        ssh_message_reply_default(message);
      }

      ssh_message_free(message);
    }

    // Cleanup
    ssh_disconnect(session);
    ssh_free(session);

    BOOST_LOG(info) << "SSH client disconnected: " << client_username;
  }

  int
  ssh_server_t::authenticate_password(void *session, const std::string &user, const std::string &pass) {
    return (user == username_ && pass == password_) ? SSH_AUTH_SUCCESS : SSH_AUTH_DENIED;
  }

  int
  init() {
    ssh_init();
    global_server = std::make_shared<ssh_server_t>();
    return 0;
  }

  std::shared_ptr<ssh_server_t>
  get_server() {
    return global_server;
  }

}  // namespace ssh_server
