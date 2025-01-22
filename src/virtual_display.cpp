/**
 * @file src/virtual_display.cpp
 * @brief virtual_display.h is a header file for the virtual display.
 */
#include "virtual_display.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace virtual_display {
  bool
  isMonitorActive() {
    int monitorCount = 0;
    BOOL monitorActive = FALSE;

    auto monitorEnumProc = [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
      int *count = reinterpret_cast<int *>(dwData);
      (*count)++;

      // Retrieve monitor information
      MONITORINFOEX monitorInfo;
      monitorInfo.cbSize = sizeof(MONITORINFOEX);
      if (GetMonitorInfo(hMonitor, &monitorInfo)) {
        std::cout << "Monitor Name: " << monitorInfo.szDevice << std::endl;
        std::cout << "Monitor Dimensions: "
                  << "Left: " << monitorInfo.rcMonitor.left << ", "
                  << "Top: " << monitorInfo.rcMonitor.top << ", "
                  << "Right: " << monitorInfo.rcMonitor.right << ", "
                  << "Bottom: " << monitorInfo.rcMonitor.bottom << std::endl;

        if (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) {
          std::cout << "This is the primary monitor." << std::endl;
        }
      }

      return TRUE;
    };

    monitorActive = EnumDisplayMonitors(NULL, NULL, monitorEnumProc, reinterpret_cast<LPARAM>(&monitorCount));

    if (monitorActive && monitorCount > 0) {
      std::cout << "Number of monitors detected: " << monitorCount << std::endl;
      return true;
    }
    else {
      std::cout << "No active monitors detected." << std::endl;
      return false;
    }
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
      std::cerr << "Failed to change resolution. Error code: " << result << std::endl;
      return false;
    }
  }

  bool
  check_resolution() {
    DEVMODE currentMode = {};
    currentMode.dmSize = sizeof(currentMode);
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &currentMode)) {
      std::cout << "Current Resolution: "
                << currentMode.dmPelsWidth << "x" << currentMode.dmPelsHeight
                << "@" << currentMode.dmDisplayFrequency << "Hz" << std::endl;

      // 현재 해상도가 800x600인지 확인
      if (currentMode.dmPelsWidth < 1000 && currentMode.dmPelsHeight < 800) {
        std::cout << "Resolution is 800x600. Changing to 1920x1080 @ 60Hz..." << std::endl;

        // 해상도 변경
        if (change_resolution(1920, 1080, 60)) {
          std::cout << "Resolution changed to 1920x1080 @ 60Hz successfully." << std::endl;
        }
        else {
          std::cerr << "Resolution change failed." << std::endl;
        }
      }
      else if (currentMode.dmPelsHeight >= 2000) {
        std::cout << "Resolution is 3840x2160. Changing to 1920x1080 @ 60Hz..." << std::endl;
        // 해상도 변경
        if (change_resolution(1920, 1080, 60)) {
          std::cout << "Resolution changed to 1920x1080 @ 60Hz successfully." << std::endl;
        }
        else {
          std::cerr << "Resolution change failed." << std::endl;
        }
      }
      else {
        std::cout << "Resolution is not 800x600. No changes required." << std::endl;
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
    std::string command = enable ? "Get-PnpDevice | Where-Object { $_.FriendlyName -like \\\"*Virtual Display Drive*\\\" } | Enable-PnpDevice -Confirm:$false -ErrorAction Stop" : "Get-PnpDevice | Where-Object { $_.FriendlyName -like \\\"*Virtual Display Drive*\\\" } | Disable-PnpDevice -Confirm:$false -ErrorAction Stop";
    std::string result = execute_inline_powerShell(command);
    std::cout << result << std::endl;
    return true;
  }
  bool
  exist_virtual_display() {
    std::string command = "Get-PnpDevice | Where-Object { $_.FriendlyName -like \\\"*Virtual Display Drive*\\\" } -ErrorAction Stop";
    std::string result = execute_inline_powerShell(command);

    std::istringstream stream(result);
    std::string line;
    while (std::getline(stream, line)) {
      // Check if the line contains information about a monitor and it's active
      std::cout << line << std::endl;
      if (line.find("Display") != std::string::npos && line.find("OK") != std::string::npos) {
        return true;
      }
    }
    return false;
  }
  // Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Virtual Display Drive*" } -ErrorAction Stop
  // Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Virtual Display Drive*" } | Disable-PnpDevice -Confirm:$false -ErrorAction Stop
  // Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Virtual Display Drive*" } | Enable-PnpDevice -Confirm:$false -ErrorAction Stop
}  // namespace virtual_display
