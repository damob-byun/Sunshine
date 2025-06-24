/**
 * @file src/virtual_display.h
 * @brief virtual_display.h is a header file for the virtual display.
 */


#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include <windows.h>

#include <cfgmgr32.h>
#include <SetupApi.h>


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
  //const static char *VDD_DISPLAY_ID = "PSCCDD0";  // You will see it in registry (HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY)
  //const static char *VDD_DISPLAY_NAME = "ParsecVDA";  // You will see it in the [Advanced display settings] tab.

  // Apdater GUID to obtain the device handle.
  // {00b41627-04c4-429e-a26e-0265cf50c8fa}
  const static GUID VDD_ADAPTER_GUID = { 0x00b41627, 0x04c4, 0x429e, { 0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa } };
  //const static char *VDD_ADAPTER_NAME = "Parsec Virtual Display Adapter";

  // Class and hwid to query device status.
  // {4d36e968-e325-11ce-bfc1-08002be10318}
  const static GUID VDD_CLASS_GUID = { 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
  const static char *VDD_HARDWARE_ID = "Root\\Parsec\\VDA";

  // Actually up to 16 devices could be created per adapter
  //  so just use a half to avoid plugging lag.
  const static int VDD_MAX_DISPLAYS = 8;

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
     
  extern HANDLE global_vdd;
    extern std::atomic<bool> vdd_update_running;
    extern std::thread vdd_update_worker;
}  // namespace virtual_display
