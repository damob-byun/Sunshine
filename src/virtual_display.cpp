/**
 * @file src/virtual_display.cpp
 * @brief virtual_display.h is a header file for the virtual display.
 */
#include "virtual_display.h"
#include <windows.h>
#include <tlhelp32.h>
#include "logging.h"
#include "platform/windows/misc.h"
namespace virtual_display {
  HANDLE global_vdd = NULL;
  std::atomic<bool> vdd_update_running { true };
  std::thread vdd_update_worker;

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

  void
  vdd_update_thread(std::atomic<bool> &running) {
     int count = 0;
    while (running) {
      vdd_update(global_vdd);
      if (count++ > 6) {
        count = 0;
        DWORD pid = GetProcessIdByName("WmCLt.exe");
        if (pid != 0) {
          EnumWindows(EnumWindowsProc, (LPARAM) pid);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
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

  // 프로세스 이름으로 PID 얻기
  DWORD
  GetProcessIdByName(const std::string &processName) {
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32 pe;
      pe.dwSize = sizeof(pe);
      if (Process32First(hSnap, &pe)) {
        do {
          if (processName == pe.szExeFile) {
            pid = pe.th32ProcessID;
            break;
          }
        } while (Process32Next(hSnap, &pe));
      }
      CloseHandle(hSnap);
    }
    return pid;
  }

  // 윈도우 핸들 콜백
  BOOL CALLBACK
  EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD) lParam) {
      ShowWindow(hwnd, SW_HIDE);  // 윈도우 숨김
    }
    return TRUE;
  }

  // 사용 예시
  void
  HideWindowsOfProcess(const std::string &processName) {
    DWORD pid = GetProcessIdByName(processName);
    if (pid != 0) {
      EnumWindows(EnumWindowsProc, (LPARAM) pid);
    }
  }

}  // namespace virtual_display
