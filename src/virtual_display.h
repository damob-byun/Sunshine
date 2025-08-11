/**
 * @file src/virtual_display.h
 * @brief virtual_display.h is a header file for the virtual display.
 */

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <SetupApi.h>
#include <cfgmgr32.h>

namespace virtual_display {
  // Device helper.
  //////////////////////////////////////////////////

  typedef enum {
    DEVICE_OK = 0,  // Ready to use
    DEVICE_INACCESSIBLE,  // Inaccessible
    DEVICE_UNKNOWN,  // Unknown status
    DEVICE_UNKNOWN_PROBLEM,  // Unknown problem
    DEVICE_DISABLED,  // Device is disabled
    DEVICE_DRIVER_ERROR,  // Device encountered error
    DEVICE_RESTART_REQUIRED,  // Must restart PC to use (could ignore but would have issue)
    DEVICE_DISABLED_SERVICE,  // Service is disabled
    DEVICE_NOT_INSTALLED  // Driver is not installed
  } DeviceStatus;

  // Parsec VDD core.
  //////////////////////////////////////////////////

  // Display name info.

  // Core IoControl codes, see usage below.
  typedef enum {
    VDD_IOCTL_ADD = 0x0022e004,  // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 1, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
    VDD_IOCTL_REMOVE = 0x0022a008,  // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
    VDD_IOCTL_UPDATE = 0x0022a00c,  // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)
    VDD_IOCTL_VERSION = 0x0022e010,  // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 4, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

    // new code in driver v0.45
    // relates to IOCTL_UPDATE and per display state
    // but unused in Parsec app
    VDD_IOCTL_UNKONWN = 0x0022a00c,  // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 5, METHOD_BUFFERED, FILE_WRITE_ACCESS)
  } VddCtlCode;

  bool
  isMonitorActive();
  bool
  change_resolution(int width, int height, int refreshRate);
  bool
  check_resolution();
  std::string
  execute_inline_powerShell(const std::string &command);
  bool
  toggle_virtual_display(bool enable);
  bool
  exist_virtual_display();

  void
  close_device_handle(HANDLE handle);

  HANDLE
  open_device_handle(const GUID *interfaceGuid);

  DeviceStatus
  query_device_status(const GUID *classGuid, const char *deviceId);

  void
  vdd_remove_display(HANDLE vdd, int index);

  int
  vdd_add_display(HANDLE vdd);

  void
  vdd_update(HANDLE vdd);

  int
  vdd_version(HANDLE vdd);

  DWORD
  vdd_io_control(HANDLE vdd, VddCtlCode code, const void *data, size_t size);

  void
  start_hiddener();

  extern HANDLE global_vdd;
  extern std::atomic<bool> vdd_update_running;
  extern std::thread vdd_update_worker;
  

}  // namespace virtual_display
