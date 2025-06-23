/**
 * @file src/virtual_display.cpp
 * @brief virtual_display.h is a header file for the virtual display.
 */
#include "virtual_display.h"
#include <iostream>
#include <stdexcept>
#include <dxgi1_2.h>
#include <windows.h>
#include "logging.h"
#include "platform/windows/misc.h"

namespace virtual_display {
  
bool isMonitorActive() {

    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
        std::cerr << "Failed to create DXGIFactory1." << std::endl;
        return false;
    }

    bool foundActive = false;
    UINT adapterIndex = 0;
    IDXGIAdapter1* pAdapter = nullptr;
    while (pFactory->EnumAdapters1(adapterIndex, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 adapterDesc;
        if (SUCCEEDED(pAdapter->GetDesc1(&adapterDesc))) {
            // platf::to_utf8로 변환 후 비교
            std::string desc_utf8 = platf::to_utf8(adapterDesc.Description);
            BOOST_LOG(info) << "VDD: Adapter " << adapterIndex << ": " << desc_utf8;
            if (desc_utf8.find("Microsoft Basic Render Driver") != std::string::npos) {
                pAdapter->Release();
                adapterIndex++;
                continue;
            }
        }

        UINT outputIndex = 0;
        IDXGIOutput* pOutput = nullptr;
        while (pAdapter->EnumOutputs(outputIndex, &pOutput) != DXGI_ERROR_NOT_FOUND) {
            DXGI_OUTPUT_DESC outputDesc;
            if (SUCCEEDED(pOutput->GetDesc(&outputDesc))) {
                if (outputDesc.AttachedToDesktop) {
                    foundActive = true;
                }
            }
            pOutput->Release();
            outputIndex++;
        }
        pAdapter->Release();
        adapterIndex++;
    }
    pFactory->Release();
    return foundActive;
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
      std::cerr << "VDD: Failed to change resolution. Error code: " << result << std::endl;
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
          std::cerr << "Resolution change failed." << std::endl;
        }
      }
      else if (currentMode.dmPelsHeight >= 2000) {
        BOOST_LOG(info) << "Resolution is 3840x2160. Changing to 1920x1080 @ 60Hz..." << std::endl;
        // 해상도 변경
        if (change_resolution(1920, 1080, 60)) {
          BOOST_LOG(info) << "Resolution changed to 1920x1080 @ 60Hz successfully." << std::endl;
        }
        else {
          std::cerr << "Resolution change failed." << std::endl;
        }
      }
      else {
        BOOST_LOG(info) << "Resolution is not 800x600. No changes required." << std::endl;
      }
    }
    else {
      std::cerr << "Failed to get current display settings." << std::endl;
    }
    return true;
  }
  std::string
  execute_inline_powerShell(const std::string &command) {
    std::string fullCommand = "powershell.exe -ExecutionPolicy Bypass -Command \"" + command + "\"";

    std::string result;
    HANDLE hStdOutRead, hStdOutWrite;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
      throw std::runtime_error("Failed to create pipe.");
    }

    if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
      throw std::runtime_error("Failed to set handle information.");
    }

    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFO si = { 0 };
    si.cb = sizeof(STARTUPINFO);
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    if (!CreateProcess(NULL, const_cast<char *>(fullCommand.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
      throw std::runtime_error("Failed to create process.");
    }

    CloseHandle(hStdOutWrite);

    char buffer[128];
    DWORD bytesRead;
    while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
      buffer[bytesRead] = '\0';
      result += buffer;
    }

    CloseHandle(hStdOutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
  }
  bool
  toggle_virtual_display(bool enable) {
    if (displays.size() > 0 && !enable) {
      int index = displays.back();
      vdd_remove_display(global_vdd, index);
      displays.pop_back();
      BOOST_LOG(warning) << "VDD: Removed the last virtual display, index: " << index;

      return true;
    }
    if(displays.size() > 0  && enable) {
      BOOST_LOG(warning) << "VDD: Already one added, cannot add more.";
      return false;
    }
    else {
      if (displays.size() < VDD_MAX_DISPLAYS && enable) {
        int index = vdd_add_display(global_vdd);
        if (index != -1) {
          displays.push_back(index);
          BOOST_LOG(warning) <<  "VDD: Added a new virtual display, index: " <<  index;
        }
        else {
          BOOST_LOG(warning) << "VDD: Add virtual display failed.";
        }
        return true;
      }
    }
    close_device_handle(global_vdd);
    global_vdd = NULL;
    return false;
  }
  bool
  exist_virtual_display() {
    if(global_vdd != NULL && global_vdd != INVALID_HANDLE_VALUE) {
      // Check if the device is still OK.
      BOOST_LOG(warning) << "VDD: VDD device already exists, no need to reinitialize.";
      return true;
    }
    DeviceStatus status = query_device_status(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    if (status != DEVICE_OK) {
      BOOST_LOG(warning) << "VDD: VDD device is not OK, got status "  << status;
      return false;
    }

    // Obtain device handle.
    global_vdd = open_device_handle(&VDD_ADAPTER_GUID);
    if (global_vdd == NULL || global_vdd == INVALID_HANDLE_VALUE) {
      BOOST_LOG(warning) << "VDD: Failed to obtain the device handle " ;
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
