/**
 * @file src/ssh_server.h
 * @brief SSH server implementation for USB/IP port forwarding
 */
#pragma once

#include <memory>
#include <string>
#include <functional>
#include <map>
#include <thread>
#include <atomic>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
#endif

namespace ssh_server {

  /**
   * @brief Structure to hold USB/IP tunnel information
   */
  struct tunnel_info_t {
    std::string client_id;  // Unique identifier for the SSH client
    std::string client_ip;  // Client's IP address
    int forwarded_port;  // Local port that was forwarded by SSH tunnel
    int remote_usbip_port;  // Remote USB/IP server port on client
    bool active;  // Whether the tunnel is currently active
  };

  /**
   * @brief SSH Server class for handling client connections and port forwarding
   */
  class ssh_server_t {
  public:
    ssh_server_t();
    ~ssh_server_t();

    /**
     * @brief Initialize and start the SSH server
     * @param port Port to bind (0 for dynamic allocation)
     * @param username SSH authentication username
     * @param password SSH authentication password
     * @return true if successful, false otherwise
     */
    bool start(int port, const std::string &username, const std::string &password);

    /**
     * @brief Stop the SSH server
     */
    void stop();

    /**
     * @brief Check if the server is running
     */
    bool is_running() const;

    /**
     * @brief Get the port the server is listening on
     */
    int get_port() const;

    /**
     * @brief Get the generated password
     */
    std::string get_password() const;

    /**
     * @brief Get list of active tunnels
     */
    std::vector<tunnel_info_t> get_active_tunnels() const;

    /**
     * @brief Get tunnel info by client ID
     */
    std::shared_ptr<tunnel_info_t> get_tunnel(const std::string &client_id) const;

    /**
     * @brief Set callback for when a new tunnel is established
     */
    void set_tunnel_callback(std::function<void(const tunnel_info_t &)> callback);

  private:
    class impl;
    std::unique_ptr<impl> pimpl;

    void server_thread();
    void handle_client(void *session);
    int authenticate_password(void *session, const std::string &user, const std::string &pass);
    int handle_channel_open(void *session);
    int handle_port_forward_request(void *session, void *channel, int requested_port);

    std::thread server_thread_;
    std::atomic<bool> running_;
    int listen_port_;
    std::string username_;
    std::string password_;

    // Map of client_id to tunnel_info
    std::map<std::string, std::shared_ptr<tunnel_info_t>> tunnels_;
    mutable std::mutex tunnels_mutex_;

    std::function<void(const tunnel_info_t &)> tunnel_callback_;
  };

  /**
   * @brief Initialize SSH server subsystem
   */
  int init();

  /**
   * @brief Get the global SSH server instance
   */
  std::shared_ptr<ssh_server_t> get_server();

}  // namespace ssh_server
