/**
 * @file src/usbip_client.cpp
 * @brief USB/IP client implementation for remote USB device mounting
 */

#include "usbip_client.h"
#include "logging.h"

#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #define CLOSE_SOCKET closesocket
  #define SOCKET_ERROR_CODE WSAGetLastError()
#else
  #include <arpa/inet.h>
  #include <unistd.h>
  #define CLOSE_SOCKET close
  #define SOCKET_ERROR_CODE errno
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR -1
#endif

namespace usbip_client {

  // Global manager instance
  static std::shared_ptr<usbip_manager_t> global_manager;

  class usbip_client_t::impl {
  public:
    impl() {}
    ~impl() {}
  };

  usbip_client_t::usbip_client_t():
      pimpl(std::make_unique<impl>()),
      socket_fd_(INVALID_SOCKET),
      connected_(false),
      port_(0) {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
  }

  usbip_client_t::~usbip_client_t() {
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
  }

  bool
  usbip_client_t::connect(const std::string &host, int port) {
    if (connected_) {
      BOOST_LOG(warning) << "USB/IP client is already connected";
      return false;
    }

    host_ = host;
    port_ = port;

    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == INVALID_SOCKET) {
      BOOST_LOG(error) << "Failed to create socket: " << SOCKET_ERROR_CODE;
      return false;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert hostname to IP address
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
      // Try to resolve hostname
      struct addrinfo hints, *result;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;

      int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
      if (ret != 0) {
        BOOST_LOG(error) << "Failed to resolve hostname: " << host;
        CLOSE_SOCKET(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
        return false;
      }

      memcpy(&server_addr.sin_addr,
        &((struct sockaddr_in *) result->ai_addr)->sin_addr,
        sizeof(struct in_addr));
      freeaddrinfo(result);
    }

    // Connect to server
    if (::connect(socket_fd_, (struct sockaddr *) &server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
      BOOST_LOG(error) << "Failed to connect to USB/IP server " << host << ":" << port
                       << " - Error: " << SOCKET_ERROR_CODE;
      CLOSE_SOCKET(socket_fd_);
      socket_fd_ = INVALID_SOCKET;
      return false;
    }

    connected_ = true;
    BOOST_LOG(info) << "Connected to USB/IP server " << host << ":" << port;
    return true;
  }

  void
  usbip_client_t::disconnect() {
    if (!connected_) {
      return;
    }

    if (socket_fd_ != INVALID_SOCKET) {
      CLOSE_SOCKET(socket_fd_);
      socket_fd_ = INVALID_SOCKET;
    }

    connected_ = false;
    BOOST_LOG(info) << "Disconnected from USB/IP server";
  }

  bool
  usbip_client_t::is_connected() const {
    return connected_;
  }

  std::vector<usb_device_info_t>
  usbip_client_t::get_device_list() {
    std::vector<usb_device_info_t> devices;

    if (!connected_) {
      BOOST_LOG(error) << "Not connected to USB/IP server";
      return devices;
    }

    if (!send_op_req_devlist()) {
      BOOST_LOG(error) << "Failed to send device list request";
      return devices;
    }

    if (!recv_op_rep_devlist(devices)) {
      BOOST_LOG(error) << "Failed to receive device list response";
      return devices;
    }

    return devices;
  }

  bool
  usbip_client_t::import_device(const std::string &busid) {
    if (!connected_) {
      BOOST_LOG(error) << "Not connected to USB/IP server";
      return false;
    }

    if (!send_op_req_import(busid)) {
      BOOST_LOG(error) << "Failed to send import request for " << busid;
      return false;
    }

    if (!recv_op_rep_import()) {
      BOOST_LOG(error) << "Failed to receive import response for " << busid;
      return false;
    }

    imported_devices_.push_back(busid);
    BOOST_LOG(info) << "Successfully imported USB device: " << busid;
    return true;
  }

  bool
  usbip_client_t::export_device(const std::string &busid) {
    auto it = std::find(imported_devices_.begin(), imported_devices_.end(), busid);
    if (it == imported_devices_.end()) {
      BOOST_LOG(warning) << "Device " << busid << " is not imported";
      return false;
    }

    // Note: USB/IP doesn't have an explicit "export" command
    // Devices are typically detached by closing the connection or using vhci-hcd detach
    // For now, we just remove it from our list
    imported_devices_.erase(it);
    BOOST_LOG(info) << "Marked device for export: " << busid;
    return true;
  }

  std::vector<std::string>
  usbip_client_t::get_imported_devices() const {
    return imported_devices_;
  }

  bool
  usbip_client_t::send_op_req_devlist() {
    // USB/IP OP_REQ_DEVLIST packet structure
    struct {
      uint16_t version;
      uint16_t command;
      uint32_t status;
    } __attribute__((packed)) request;

    request.version = htons(USBIP_VERSION);
    request.command = htons(OP_REQ_DEVLIST);
    request.status = 0;

    int sent = send(socket_fd_, (const char *) &request, sizeof(request), 0);
    if (sent != sizeof(request)) {
      BOOST_LOG(error) << "Failed to send OP_REQ_DEVLIST";
      return false;
    }

    return true;
  }

  bool
  usbip_client_t::recv_op_rep_devlist(std::vector<usb_device_info_t> &devices) {
    // USB/IP OP_REP_DEVLIST packet structure
    struct {
      uint16_t version;
      uint16_t command;
      uint32_t status;
      uint32_t num_devices;
    } __attribute__((packed)) reply;

    int received = recv(socket_fd_, (char *) &reply, sizeof(reply), 0);
    if (received != sizeof(reply)) {
      BOOST_LOG(error) << "Failed to receive OP_REP_DEVLIST header";
      return false;
    }

    reply.version = ntohs(reply.version);
    reply.command = ntohs(reply.command);
    reply.status = ntohl(reply.status);
    reply.num_devices = ntohl(reply.num_devices);

    if (reply.command != OP_REP_DEVLIST) {
      BOOST_LOG(error) << "Invalid reply command: " << reply.command;
      return false;
    }

    if (reply.status != 0) {
      BOOST_LOG(error) << "Device list request failed with status: " << reply.status;
      return false;
    }

    BOOST_LOG(info) << "USB/IP server reports " << reply.num_devices << " devices";

    // For each device, receive device info
    for (uint32_t i = 0; i < reply.num_devices; ++i) {
      // USB/IP device info structure
      struct {
        char path[256];
        char busid[32];
        uint32_t busnum;
        uint32_t devnum;
        uint32_t speed;
        uint16_t idVendor;
        uint16_t idProduct;
        uint16_t bcdDevice;
        uint8_t bDeviceClass;
        uint8_t bDeviceSubClass;
        uint8_t bDeviceProtocol;
        uint8_t bConfigurationValue;
        uint8_t bNumConfigurations;
        uint8_t bNumInterfaces;
      } __attribute__((packed)) dev_info;

      received = recv(socket_fd_, (char *) &dev_info, sizeof(dev_info), 0);
      if (received != sizeof(dev_info)) {
        BOOST_LOG(error) << "Failed to receive device info";
        continue;
      }

      usb_device_info_t device;
      device.path = std::string(dev_info.path);
      device.busid = std::string(dev_info.busid);
      device.busnum = ntohl(dev_info.busnum);
      device.devnum = ntohl(dev_info.devnum);
      device.speed = ntohl(dev_info.speed);
      device.idVendor = ntohs(dev_info.idVendor);
      device.idProduct = ntohs(dev_info.idProduct);
      device.bcdDevice = ntohs(dev_info.bcdDevice);
      device.bDeviceClass = dev_info.bDeviceClass;
      device.bDeviceSubClass = dev_info.bDeviceSubClass;
      device.bDeviceProtocol = dev_info.bDeviceProtocol;
      device.bConfigurationValue = dev_info.bConfigurationValue;
      device.bNumConfigurations = dev_info.bNumConfigurations;
      device.bNumInterfaces = dev_info.bNumInterfaces;

      // Receive interface info (skipped for now, but must read from socket)
      for (int j = 0; j < dev_info.bNumInterfaces; ++j) {
        struct {
          uint8_t bInterfaceClass;
          uint8_t bInterfaceSubClass;
          uint8_t bInterfaceProtocol;
          uint8_t padding;
        } __attribute__((packed)) iface_info;

        recv(socket_fd_, (char *) &iface_info, sizeof(iface_info), 0);
      }

      devices.push_back(device);

      BOOST_LOG(info) << "Device " << i << ": " << device.busid
                      << " (VID:PID = " << std::hex << device.idVendor << ":" << device.idProduct << ")";
    }

    return true;
  }

  bool
  usbip_client_t::send_op_req_import(const std::string &busid) {
    // USB/IP OP_REQ_IMPORT packet structure
    struct {
      uint16_t version;
      uint16_t command;
      uint32_t status;
      char busid[32];
    } __attribute__((packed)) request;

    request.version = htons(USBIP_VERSION);
    request.command = htons(OP_REQ_IMPORT);
    request.status = 0;
    strncpy(request.busid, busid.c_str(), sizeof(request.busid) - 1);
    request.busid[sizeof(request.busid) - 1] = '\0';

    int sent = send(socket_fd_, (const char *) &request, sizeof(request), 0);
    if (sent != sizeof(request)) {
      BOOST_LOG(error) << "Failed to send OP_REQ_IMPORT";
      return false;
    }

    return true;
  }

  bool
  usbip_client_t::recv_op_rep_import() {
    // USB/IP OP_REP_IMPORT packet structure
    struct {
      uint16_t version;
      uint16_t command;
      uint32_t status;
      // ... device info follows
    } __attribute__((packed)) reply;

    int received = recv(socket_fd_, (char *) &reply, sizeof(reply), 0);
    if (received != sizeof(reply)) {
      BOOST_LOG(error) << "Failed to receive OP_REP_IMPORT header";
      return false;
    }

    reply.version = ntohs(reply.version);
    reply.command = ntohs(reply.command);
    reply.status = ntohl(reply.status);

    if (reply.command != OP_REP_IMPORT) {
      BOOST_LOG(error) << "Invalid reply command: " << reply.command;
      return false;
    }

    if (reply.status != 0) {
      BOOST_LOG(error) << "Import request failed with status: " << reply.status;
      return false;
    }

    // Device info follows but we can skip it for now
    // In a complete implementation, we would read and process the device info

    return true;
  }

  int
  init() {
    global_manager = std::make_shared<usbip_manager_t>();
    return 0;
  }

  usbip_manager_t::usbip_manager_t() {}

  usbip_manager_t::~usbip_manager_t() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.clear();
  }

  std::shared_ptr<usbip_client_t>
  usbip_manager_t::create_client(const std::string &host, int port) {
    auto client = std::make_shared<usbip_client_t>();
    if (client->connect(host, port)) {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      clients_.push_back(client);
      return client;
    }
    return nullptr;
  }

  std::vector<std::shared_ptr<usbip_client_t>>
  usbip_manager_t::get_clients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_;
  }

  void
  usbip_manager_t::remove_client(std::shared_ptr<usbip_client_t> client) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(clients_.begin(), clients_.end(), client);
    if (it != clients_.end()) {
      (*it)->disconnect();
      clients_.erase(it);
    }
  }

  std::vector<usb_device_info_t>
  usbip_manager_t::get_all_imported_devices() const {
    std::vector<usb_device_info_t> all_devices;
    std::lock_guard<std::mutex> lock(clients_mutex_);

    for (const auto &client : clients_) {
      if (client->is_connected()) {
        // Note: This would require storing device info when importing
        // For now, just return empty list
      }
    }

    return all_devices;
  }

  std::shared_ptr<usbip_manager_t>
  get_manager() {
    return global_manager;
  }

}  // namespace usbip_client
