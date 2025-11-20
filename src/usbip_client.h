/**
 * @file src/usbip_client.h
 * @brief USB/IP client implementation for remote USB device mounting
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
#endif

namespace usbip_client {

  // USB/IP protocol constants
  constexpr uint16_t USBIP_VERSION = 0x0111;

  // USB/IP command codes
  enum usbip_op_code : uint16_t {
    OP_REQ_DEVLIST = 0x8005,  // Request device list
    OP_REP_DEVLIST = 0x0005,  // Reply device list
    OP_REQ_IMPORT = 0x8003,  // Request import device
    OP_REP_IMPORT = 0x0003,  // Reply import device
    OP_REQ_DEVINFO = 0x8004,  // Request device info
    OP_REP_DEVINFO = 0x0004,  // Reply device info
  };

  // USB/IP command packet header
  struct usbip_header_basic {
    uint16_t version;
    uint16_t command;
    uint32_t status;
  } __attribute__((packed));

  /**
   * @brief USB device information
   */
  struct usb_device_info_t {
    std::string busid;  // Bus ID (e.g., "1-1")
    std::string path;  // Device path
    uint32_t busnum;  // Bus number
    uint32_t devnum;  // Device number
    uint32_t speed;  // USB speed
    uint16_t idVendor;  // Vendor ID
    uint16_t idProduct;  // Product ID
    uint16_t bcdDevice;  // Device release number
    uint8_t bDeviceClass;  // Device class
    uint8_t bDeviceSubClass;  // Device subclass
    uint8_t bDeviceProtocol;  // Device protocol
    uint8_t bConfigurationValue;  // Configuration value
    uint8_t bNumConfigurations;  // Number of configurations
    uint8_t bNumInterfaces;  // Number of interfaces
    std::string manufacturer;  // Manufacturer string
    std::string product;  // Product string
    std::string serial;  // Serial number string
  };

  /**
   * @brief USB/IP client class for connecting to remote USB/IP servers
   */
  class usbip_client_t {
  public:
    usbip_client_t();
    ~usbip_client_t();

    /**
     * @brief Connect to a USB/IP server
     * @param host Hostname or IP address
     * @param port Port number (default 3240)
     * @return true if successful, false otherwise
     */
    bool connect(const std::string &host, int port = 3240);

    /**
     * @brief Disconnect from the USB/IP server
     */
    void disconnect();

    /**
     * @brief Check if connected to a server
     */
    bool is_connected() const;

    /**
     * @brief Get list of available USB devices from the server
     * @return Vector of device information
     */
    std::vector<usb_device_info_t> get_device_list();

    /**
     * @brief Import (attach) a USB device
     * @param busid Bus ID of the device to import
     * @return true if successful, false otherwise
     */
    bool import_device(const std::string &busid);

    /**
     * @brief Export (detach) a USB device
     * @param busid Bus ID of the device to export
     * @return true if successful, false otherwise
     */
    bool export_device(const std::string &busid);

    /**
     * @brief Get list of currently imported devices
     */
    std::vector<std::string> get_imported_devices() const;

  private:
    class impl;
    std::unique_ptr<impl> pimpl;

    bool send_op_req_devlist();
    bool recv_op_rep_devlist(std::vector<usb_device_info_t> &devices);
    bool send_op_req_import(const std::string &busid);
    bool recv_op_rep_import();

    int socket_fd_;
    bool connected_;
    std::string host_;
    int port_;
    std::vector<std::string> imported_devices_;
  };

  /**
   * @brief Initialize USB/IP client subsystem
   */
  int init();

  /**
   * @brief USB/IP manager for handling multiple connections
   */
  class usbip_manager_t {
  public:
    usbip_manager_t();
    ~usbip_manager_t();

    /**
     * @brief Create a new USB/IP client connection
     * @param host Hostname or IP address
     * @param port Port number
     * @return Shared pointer to the client, or nullptr on failure
     */
    std::shared_ptr<usbip_client_t> create_client(const std::string &host, int port = 3240);

    /**
     * @brief Get all active clients
     */
    std::vector<std::shared_ptr<usbip_client_t>> get_clients() const;

    /**
     * @brief Remove a client
     */
    void remove_client(std::shared_ptr<usbip_client_t> client);

    /**
     * @brief Get all imported devices across all clients
     */
    std::vector<usb_device_info_t> get_all_imported_devices() const;

  private:
    std::vector<std::shared_ptr<usbip_client_t>> clients_;
    mutable std::mutex clients_mutex_;
  };

  /**
   * @brief Get the global USB/IP manager instance
   */
  std::shared_ptr<usbip_manager_t> get_manager();

}  // namespace usbip_client
