/**
 * @file src/virtual_display.cpp
 * @brief virtual_display.h is a header file for the virtual display.
 */
#include "virtual_display.h"
#include <tlhelp32.h>
#include <cwchar>
#include "logging.h"
#include "platform/windows/misc.h"
#include "file_handler.h"

namespace virtual_display {
  HANDLE global_vdd = NULL;
  std::atomic<bool> vdd_update_running { true };
  std::thread vdd_update_worker;
  bool hidden_done = false;

  char *VDD_DISPLAY_ID = "PSCCDD0";  // You will see it in registry (HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY)
  char *VDD_DISPLAY_NAME = "ParsecVDA";  // You will see it in the [Advanced display settings] tab.

  // Apdater GUID to obtain the device handle.
  // {00b41627-04c4-429e-a26e-0265cf50c8fa}
  GUID VDD_ADAPTER_GUID = { 0x00b41627, 0x04c4, 0x429e, { 0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa } };
  char *VDD_ADAPTER_NAME = "Parsec Virtual Display Adapter";

  // Class and hwid to query device status.
  // {4d36e968-e325-11ce-bfc1-08002be10318}
  GUID VDD_CLASS_GUID = { 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
  char *VDD_HARDWARE_ID = "Root\\Parsec\\VDA";

  // Actually up to 16 devices could be created per adapter
  //  so just use a half to avoid plugging lag.
  int VDD_MAX_DISPLAYS = 8;

  // UTF-8 to UTF-16 conversion (file-local)
  static std::wstring utf8_to_wide(const std::string &utf8) {
    if (utf8.empty()) return L"";
    int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (need <= 0) return L"";
    std::wstring wide(static_cast<size_t>(need - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), need);
    return wide;
  }

  // Find PID by executable name (UTF-8)
  static DWORD find_process_id_by_name_utf8(const std::string &exe_name_utf8) {
    std::wstring exe_name_w = utf8_to_wide(exe_name_utf8);
    if (exe_name_w.empty()) return 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W proc_entry{};
    proc_entry.dwSize = sizeof(PROCESSENTRY32W);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &proc_entry)) {
      do {
        if (_wcsicmp(proc_entry.szExeFile, exe_name_w.c_str()) == 0) {
          pid = proc_entry.th32ProcessID;
          break;
        }
      } while (Process32NextW(snapshot, &proc_entry));
    }

    CloseHandle(snapshot);
    return pid;
  }

  struct HideEnumData {
    DWORD pid;
    std::vector<HWND> hwnds;
  };

  static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM l_param) {
    auto *data = reinterpret_cast<HideEnumData *>(l_param);
    DWORD window_pid = 0;
    GetWindowThreadProcessId(hwnd, &window_pid);
    if (window_pid == data->pid && IsWindowVisible(hwnd)) {
      // top-level visible windows only
      if (GetWindow(hwnd, GW_OWNER) == nullptr) {
        data->hwnds.push_back(hwnd);
      }
    }
    return TRUE;
  }

  // Off-screen placement helpers
  static const int k_offscreen_offset = 10000;

  static RECT get_virtual_screen_rect() {
    RECT virtual_screen{};
    virtual_screen.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virtual_screen.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    virtual_screen.right = virtual_screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtual_screen.bottom = virtual_screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return virtual_screen;
  }

  static void move_off_screen(HWND hwnd) {
    RECT virtual_screen = get_virtual_screen_rect();
    int x = virtual_screen.right + k_offscreen_offset;
    int y = virtual_screen.bottom + k_offscreen_offset;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  static bool is_off_screen(HWND hwnd) {
    RECT virtual_screen = get_virtual_screen_rect();
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return false;
    return (rect.left >= virtual_screen.right + k_offscreen_offset - 1) &&
           (rect.top  >= virtual_screen.bottom + k_offscreen_offset - 1);
  }

  static const int k_sleep_ms = 100;  // Update interval for VDD
  void
  vdd_update_thread(std::atomic<bool> &running) {
    // hiddener state
    int count = 0;
    while (running) {
      // Keep VDD alive
      vdd_update(global_vdd);
      //5분마다 hiddeon_done = true;
      if (count++ >= 3000) {
        
        count = 0;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(k_sleep_ms));
    }
  }
  void
  start_hiddener() {
    std::string hiddener_path = "\""+file_handler::get_self_path()+"\\hiddener.exe"+"\"";
    #ifdef _WIN32
    // DETACHED_PROCESS 플래그를 사용하여 독립적인 프로세스로 실행
    //std::wstring wpath_cmd = platf::from_utf8(updater_path);
    BOOST_LOG(info) << "Start hiddener : " << hiddener << std::endl;
    auto working_dir = boost::filesystem::path(file_handler::get_self_path());
    std::error_code ec;
    auto this_env = boost::this_process::environment();
    auto child = platf::run_command(true, false, updater_path, working_dir, this_env, nullptr, ec, nullptr);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't spawn ["sv << updater_path << "]: System: "sv << ec.message();
    }
    else {
      child.detach();
    }
    #endif
  }
  bool
  isMonitorActive() {
    DISPLAY_DEVICE G_device;
    ZeroMemory(&G_device, sizeof(G_device));
    G_device.cb = sizeof(DISPLAY_DEVICE);
    DWORD deviceNum = 0;
    while (EnumDisplayDevices(NULL, deviceNum, &G_device, 0)) {
      // wchar_t 배열 → std::wstring → std::string(utf8)
      std::string gDeviceString(G_device.DeviceString, strlen(G_device.DeviceString));
      BOOST_LOG(info) << "VDD: [" << deviceNum << "] Adapter DeviceString: " << gDeviceString
                      << ", StateFlags: 0x" << std::hex << G_device.StateFlags << std::dec;

      DISPLAY_DEVICE M_device;
      ZeroMemory(&M_device, sizeof(M_device));
      M_device.cb = sizeof(DISPLAY_DEVICE);
      DWORD monitorNum = 0;
      while (EnumDisplayDevices(G_device.DeviceName, monitorNum, &M_device, 0)) {
        std::string mDeviceString(M_device.DeviceString, strlen(M_device.DeviceString));
        /*BOOST_LOG(info) << "VDD: [" << deviceNum << "/" << monitorNum << "] Monitor DeviceString: " << mDeviceString
                        << ", StateFlags: 0x" << std::hex << M_device.StateFlags << std::dec;*/

        // 활성 모니터만 체크
        if (((M_device.StateFlags & DISPLAY_DEVICE_ACTIVE)) != 0) {
          BOOST_LOG(info) << "VDD: [" << deviceNum << "/" << monitorNum << "] DISPLAY_DEVICE_ACTIVE detected.";
          // 가상/기본 드라이버 무시
          if (mDeviceString.find("Microsoft Basic Render Driver") != std::string::npos ||
              mDeviceString.find("Generic") != std::string::npos) {
            BOOST_LOG(info) << "VDD: [" << deviceNum << "/" << monitorNum << "] Ignored (Basic Render Driver or Generic)";
            monitorNum++;
            continue;
          }
          // 실제 모니터가 연결되어 있음
          BOOST_LOG(info) << "VDD: [" << deviceNum << "/" << monitorNum << "] Physical monitor detected.";
          return true;
        }
        monitorNum++;
      }
      deviceNum++;
    }
    BOOST_LOG(info) << "VDD: No physical monitor detected.";
    return false;
  }
  bool
  isParsecVirtualDisplayPresent() {
    DISPLAY_DEVICE device;
    ZeroMemory(&device, sizeof(device));
    device.cb = sizeof(DISPLAY_DEVICE);

    DWORD deviceNum = 0;
    while (EnumDisplayDevices(NULL, deviceNum, &device, 0)) {
      std::string deviceString(device.DeviceString, strlen(device.DeviceString));

      // Parsec 가상 디스플레이는 일반적으로 "Parsec Virtual Display"라는 이름을 가집니다.
      if (deviceString.find("Parsec Virtual Display") != std::string::npos) {
        // 활성화된 경우만 true 반환
        if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0) {
          BOOST_LOG(info) << "VDD: Parsec Virtual Display detected and active.";
          return true;
        }
      }
      deviceNum++;
    }
    BOOST_LOG(info) << "VDD: Parsec Virtual Display not present.";
    return false;
  }
  bool
  change_resolution(int width, int height, int refreshRate) {
    DEVMODE dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = width;
    dm.dmPelsHeight = height;
    dm.dmBitsPerPel = 32;  // 비트 깊이 설정 (일반적으로 32비트)
    dm.dmDisplayFrequency = refreshRate;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

    LONG result = ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
    if (result == DISP_CHANGE_SUCCESSFUL) {
      return true;
    }
    else {
      BOOST_LOG(error) << "VDD: Failed to change resolution. Error code: " << result << std::endl;
      return false;
    }
  }

  bool
  check_resolution() {
    DEVMODE currentMode = {};
    currentMode.dmSize = sizeof(currentMode);
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &currentMode)) {
      BOOST_LOG(info) << "\nVDD: Current Resolution: "
                      << currentMode.dmPelsWidth << "x" << currentMode.dmPelsHeight
                      << "@" << currentMode.dmDisplayFrequency << "Hz" << std::endl;

      // 현재 해상도가 800x600인지 확인
      if (currentMode.dmPelsWidth < 1100 && currentMode.dmPelsHeight < 800) {
        BOOST_LOG(info) << "Resolution is 800x600 or 1024x768. Changing to 1920x1080 @ 60Hz..." << std::endl;

        // 해상도 변경
        if (change_resolution(1920, 1080, 60)) {
          BOOST_LOG(info) << "Resolution changed to 1920x1080 @ 60Hz successfully." << std::endl;
        }
        else {
          BOOST_LOG(error) << "Resolution change failed." << std::endl;
        }
      }
      else if (currentMode.dmPelsHeight >= 2000) {
        BOOST_LOG(info) << "Resolution is 3840x2160. Changing to 1920x1080 @ 60Hz..." << std::endl;
        // 해상도 변경
        if (change_resolution(1920, 1080, 60)) {
          BOOST_LOG(info) << "Resolution changed to 1920x1080 @ 60Hz successfully." << std::endl;
        }
        else {
          BOOST_LOG(error) << "Resolution change failed." << std::endl;
        }
      }
      else {
        BOOST_LOG(info) << "Resolution is not 800x600. No changes required." << std::endl;
      }
    }
    else {
      BOOST_LOG(error) << "Failed to get current display settings." << std::endl;
    }
    return true;
  }
  bool
  toggle_virtual_display(bool enable) {
    if (global_vdd == NULL || global_vdd == INVALID_HANDLE_VALUE) {
      BOOST_LOG(warning) << "VDD: VDD device does not exist, trying to create.";
      if (!exist_virtual_display()) {
        BOOST_LOG(warning) << "VDD: Failed to create VDD device.";
        return false;
      }
    }
    if (isParsecVirtualDisplayPresent() && !enable) {
      vdd_remove_display(global_vdd, 0);
      BOOST_LOG(warning) << "VDD: Removed the last virtual display, index: 0 ";

      return true;
    }
    if (isParsecVirtualDisplayPresent() && enable) {
      BOOST_LOG(warning) << "VDD: Already one added, cannot add more.";
      return false;
    }
    else {
      if (!isParsecVirtualDisplayPresent() && enable) {
        int index = vdd_add_display(global_vdd);
        
        if (!vdd_update_worker.joinable()) {
          vdd_update_running = true;
          vdd_update_worker = std::thread(vdd_update_thread, std::ref(vdd_update_running));
        }

        if (index != -1) {
          BOOST_LOG(warning) << "VDD: Added a new virtual display, index: " << index;
        }
        else {
          BOOST_LOG(warning) << "VDD: Add virtual display failed.";
        }
        return true;
      }
    }
    // close_device_handle(global_vdd);
    // global_vdd = NULL;
    return false;
  }
  bool
  exist_virtual_display() {
    if (global_vdd != NULL && global_vdd != INVALID_HANDLE_VALUE) {
      // Check if the device is still OK.
      BOOST_LOG(warning) << "VDD: VDD device already exists, no need to reinitialize.";
      return true;
    }
    DeviceStatus status = query_device_status(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    if (status != DEVICE_OK) {
      BOOST_LOG(warning) << "VDD: VDD device is not OK, got status " << status;
      return false;
    }

    // Obtain device handle.
    global_vdd = open_device_handle(&VDD_ADAPTER_GUID);
    if (global_vdd == NULL || global_vdd == INVALID_HANDLE_VALUE) {
      BOOST_LOG(warning) << "VDD: Failed to obtain the device handle ";
      return false;
    }
    return true;
  }
  // Generic DeviceIoControl for all IoControl codes.
  DWORD
  vdd_io_control(HANDLE vdd, VddCtlCode code, const void *data, size_t size) {
    if (vdd == NULL || vdd == INVALID_HANDLE_VALUE)
      return -1;

    BYTE InBuffer[32];
    ZeroMemory(InBuffer, sizeof(InBuffer));

    OVERLAPPED Overlapped;
    ZeroMemory(&Overlapped, sizeof(OVERLAPPED));

    DWORD OutBuffer = 0;
    DWORD NumberOfBytesTransferred;

    if (data != NULL && size > 0)
      memcpy(InBuffer, data, (size < sizeof(InBuffer)) ? size : sizeof(InBuffer));

    Overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DeviceIoControl(vdd, (DWORD) code, InBuffer, sizeof(InBuffer), &OutBuffer, sizeof(DWORD), NULL, &Overlapped);

    if (!GetOverlappedResultEx(vdd, &Overlapped, &NumberOfBytesTransferred, 5000, FALSE)) {
      CloseHandle(Overlapped.hEvent);
      return -1;
    }

    if (Overlapped.hEvent != NULL)
      CloseHandle(Overlapped.hEvent);

    return OutBuffer;
  }

  /**
   * Query VDD minor version.
   *
   * @param vdd The device handle of VDD.
   * @return The number of minor version.
   */
  int
  vdd_version(HANDLE vdd) {
    int minor = vdd_io_control(vdd, VDD_IOCTL_VERSION, NULL, 0);
    return minor;
  }

  /**
   * Update/ping to VDD.
   * Should call this function in a side thread for each
   *   less than 100ms to keep all added virtual displays alive.
   *
   * @param vdd The device handle of VDD.
   */
  void
  vdd_update(HANDLE vdd) {
    vdd_io_control(vdd, VDD_IOCTL_UPDATE, NULL, 0);
  }

  /**
   * Add/plug a virtual display.
   *
   * @param vdd The device handle of VDD.
   * @return The index of the added display.
   */
  int
  vdd_add_display(HANDLE vdd) {
    int idx = vdd_io_control(vdd, VDD_IOCTL_ADD, NULL, 0);
    vdd_update(vdd);

    return idx;
  }

  /**
   * Remove/unplug a virtual display.
   *
   * @param vdd The device handle of VDD.
   * @param index The index of the display will be removed.
   */
  void
  vdd_remove_display(HANDLE vdd, int index) {
    // 16-bit BE index
    UINT16 indexData = ((index & 0xFF) << 8) | ((index >> 8) & 0xFF);

    vdd_io_control(vdd, VDD_IOCTL_REMOVE, &indexData, sizeof(indexData));
    vdd_update(vdd);
  }

  /**
   * Query the driver status.
   *
   * @param classGuid The GUID of the class.
   * @param deviceId The device/hardware ID of the driver.
   * @return DeviceStatus
   */
  DeviceStatus
  query_device_status(const GUID *classGuid, const char *deviceId) {
    DeviceStatus status = DEVICE_INACCESSIBLE;

    SP_DEVINFO_DATA devInfoData;
    ZeroMemory(&devInfoData, sizeof(SP_DEVINFO_DATA));
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    HDEVINFO devInfo = SetupDiGetClassDevsA(classGuid, NULL, NULL, DIGCF_PRESENT);

    if (devInfo != INVALID_HANDLE_VALUE) {
      BOOL foundProp = FALSE;
      UINT deviceIndex = 0;

      do {
        if (!SetupDiEnumDeviceInfo(devInfo, deviceIndex, &devInfoData))
          break;

        DWORD requiredSize = 0;
        SetupDiGetDeviceRegistryPropertyA(devInfo, &devInfoData,
          SPDRP_HARDWAREID, NULL, NULL, 0, &requiredSize);

        if (requiredSize > 0) {
          DWORD regDataType = 0;
          LPBYTE propBuffer = (LPBYTE) calloc(1, requiredSize);

          if (SetupDiGetDeviceRegistryPropertyA(
                devInfo,
                &devInfoData,
                SPDRP_HARDWAREID,
                &regDataType,
                propBuffer,
                requiredSize,
                &requiredSize)) {
            if (regDataType == REG_SZ || regDataType == REG_MULTI_SZ) {
              for (LPCSTR cp = (LPCSTR) propBuffer;; cp += lstrlenA(cp) + 1) {
                if (!cp || *cp == 0 || cp >= (LPCSTR) (propBuffer + requiredSize)) {
                  status = DEVICE_NOT_INSTALLED;
                  goto except;
                }

                if (lstrcmpA(deviceId, cp) == 0)
                  break;
              }

              foundProp = TRUE;
              ULONG devStatus, devProblemNum;

              if (CM_Get_DevNode_Status(&devStatus, &devProblemNum, devInfoData.DevInst, 0) != CR_SUCCESS) {
                status = DEVICE_NOT_INSTALLED;
                goto except;
              }

              if ((devStatus & (DN_DRIVER_LOADED | DN_STARTED)) != 0) {
                status = DEVICE_OK;
              }
              else if ((devStatus & DN_HAS_PROBLEM) != 0) {
                switch (devProblemNum) {
                  case CM_PROB_NEED_RESTART:
                    status = DEVICE_RESTART_REQUIRED;
                    break;
                  case CM_PROB_DISABLED:
                  case CM_PROB_HARDWARE_DISABLED:
                    status = DEVICE_DISABLED;
                    break;
                  case CM_PROB_DISABLED_SERVICE:
                    status = DEVICE_DISABLED_SERVICE;
                    break;
                  default:
                    if (devProblemNum == CM_PROB_FAILED_POST_START)
                      status = DEVICE_DRIVER_ERROR;
                    else
                      status = DEVICE_UNKNOWN_PROBLEM;
                    break;
                }
              }
              else {
                status = DEVICE_UNKNOWN;
              }
            }
          }

        except:
          free(propBuffer);
        }

        ++deviceIndex;
      } while (!foundProp);

      if (!foundProp && GetLastError() != 0)
        status = DEVICE_NOT_INSTALLED;

      SetupDiDestroyDeviceInfoList(devInfo);
    }

    return status;
  }

  /**
   * Obtain the device handle.
   * Returns NULL or INVALID_HANDLE_VALUE if fails, otherwise a valid handle.
   * Should call close_device_handle to close this handle after use.
   *
   * @param interfaceGuid The adapter/interface GUID of the target device.
   * @return HANDLE
   */
  HANDLE
  open_device_handle(const GUID *interfaceGuid) {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HDEVINFO devInfo = SetupDiGetClassDevsA(interfaceGuid,
      NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo != INVALID_HANDLE_VALUE) {
      SP_DEVICE_INTERFACE_DATA devInterface;
      ZeroMemory(&devInterface, sizeof(SP_DEVICE_INTERFACE_DATA));
      devInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

      for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, interfaceGuid, i, &devInterface); ++i) {
        DWORD detailSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &devInterface, NULL, 0, &detailSize, NULL);

        SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *) calloc(1, detailSize);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devInterface, detail, detailSize, &detailSize, NULL)) {
          handle = CreateFileA(detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH,
            NULL);

          if (handle != NULL && handle != INVALID_HANDLE_VALUE)
            break;
        }

        free(detail);
      }

      SetupDiDestroyDeviceInfoList(devInfo);
    }

    return handle;
  }

  /* Release the device handle */
  void
  close_device_handle(HANDLE handle) {
    if (handle != NULL && handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }


}  // namespace virtual_display
