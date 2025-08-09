
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <string>
#include <thread>
#include <tlhelp32.h>

#include <tlhelp32.h>
#include <vector>
#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;
#ifdef UNICODE
std::wstring
StringToTString(const std::string &str) {
  int size_needed = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
  std::wstring wstr(size_needed, 0);
  MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], size_needed);
  return wstr;
}
#else
std::string
StringToTString(const std::string &str) {
  return str;  // 멀티바이트 환경에서는 변환 없이 그대로 사용
}
#endif

bool
stop_service(std::string service_name) {
  SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!hSCManager) {
    std::wcerr << L"OpenSCManager 실패: " << GetLastError() << std::endl;
    return false;
  }
  std::basic_string<TCHAR> tService_name = StringToTString(service_name);
  LPTSTR reqServiceName = const_cast<LPTSTR>(tService_name.data());

  SC_HANDLE hService = OpenService(hSCManager, reqServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!hService) {
    std::wcerr << L"OpenService 실패: " << GetLastError() << std::endl;
    CloseServiceHandle(hSCManager);
    return false;
  }

  SERVICE_STATUS status;
  bool result = ControlService(hService, SERVICE_CONTROL_STOP, &status);
  CloseServiceHandle(hService);
  CloseServiceHandle(hSCManager);
  return result;
}

bool
start_service(std::string service_name) {
  SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!hSCManager) {
    std::wcerr << L"OpenSCManager fail1: " << GetLastError() << std::endl;
    return false;
  }
  std::basic_string<TCHAR> tService_name = StringToTString(service_name);
  LPTSTR reqServiceName = const_cast<LPTSTR>(tService_name.data());

  SC_HANDLE hService = OpenService(hSCManager, reqServiceName, SERVICE_START);
  if (!hService) {
    std::wcerr << L"OpenService fail2: " << GetLastError() << std::endl;
    CloseServiceHandle(hSCManager);
    return false;
  }
  bool result = false;
  if (!StartService(hService, 0, NULL)) {
    std::cerr << "StartService fail3 : " << GetLastError() << std::endl;
    result = false;
  }
  else {
    std::cout << "Service '" << service_name << "' Started!" << std::endl;
    result = true;
  }
  CloseServiceHandle(hService);
  CloseServiceHandle(hSCManager);
  return result;
}

std::string
fix_path_for_windows(const std::string &path) {
  std::string fixed_path = path;
  std::replace(fixed_path.begin(), fixed_path.end(), '/', '\\');
  return fixed_path;
}

bool
kill_process_by_name(const std::string &process_name) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }

  PROCESSENTRY32 process_entry;
  process_entry.dwSize = sizeof(PROCESSENTRY32);

  bool process_killed = false;

  if (Process32First(snapshot, &process_entry)) {
    do {
      if (process_name == process_entry.szExeFile) {
        HANDLE process_handle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, process_entry.th32ProcessID);
        if (process_handle) {
          if (TerminateProcess(process_handle, 0)) {
            CloseHandle(process_handle);
            process_killed = true;
          }
          else {
            CloseHandle(process_handle);
          }
        }
      }
    } while (Process32Next(snapshot, &process_entry));
  }

  CloseHandle(snapshot);

  if (process_killed) {
    // 확인: 프로세스가 종료되었는지 검사
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
      if (Process32First(snapshot, &process_entry)) {
        do {
          if (process_name == process_entry.szExeFile) {
            CloseHandle(snapshot);
            return false;  // 아직 살아있음
          }
        } while (Process32Next(snapshot, &process_entry));
      }
      CloseHandle(snapshot);
    }
    return true;  // 정상적으로 종료됨
  }

  return false;
}

std::string
get_parent_directory(const std::string &path) {
  // remove any trailing path separators
  std::string trimmed_path = path;
  while (!trimmed_path.empty() && trimmed_path.back() == '/') {
    trimmed_path.pop_back();
  }

  std::filesystem::path p(trimmed_path);
  return p.parent_path().string();
}
std::string
get_self_path() {
  char path[MAX_PATH];
  GetModuleFileName(NULL, path, MAX_PATH);
  return get_parent_directory(std::string(path));
}
bool
make_directory(const std::string &path) {
  // first, check if the directory already exists
  if (std::filesystem::exists(path)) {
    return true;
  }

  return std::filesystem::create_directories(path);
}

bool
download_file(const std::string &url, const std::string &file) {
  CURL *curl = curl_easy_init();
  if (curl) {
    // sonar complains about weak ssl and tls versions
    // ideally, the setopts should go after the early returns; however sonar cannot detect the fix
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
  }
  else {
    std::cerr << "Couldn't create CURL instance " << std::endl;
    return false;
  }

  std::string file_dir = get_parent_directory(file);
  if (!make_directory(file_dir)) {
    std::cerr << "Couldn't create directory " << file_dir << std::endl;
    curl_easy_cleanup(curl);
    return false;
  }

  FILE *fp = fopen(file.c_str(), "wb");
  if (!fp) {
    std::cerr << "Couldn't open " << file << std::endl;
    curl_easy_cleanup(curl);
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

  CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    std::cerr << "Couldn't download " << url << ", code:" << result << std::endl;
  }

  curl_easy_cleanup(curl);
  fclose(fp);
  return result == CURLE_OK;
}

// 프로그램 교체 함수
bool
replace_file(const std::string &oldFilePath, const std::string &newFilePath) {
  try {
    // 기존 파일 삭제
    fs::remove(oldFilePath);

    // 새 파일을 기존 파일 위치로 이동
    fs::rename(newFilePath, oldFilePath);

    return true;
  }
  catch (const std::exception &e) {
    std::cerr << "File replacement failed: " << e.what() << std::endl;
    return false;
  }
}

bool
directory_exists(const std::string &path) {
  DWORD file_attributes = GetFileAttributes(path.c_str());
  if (file_attributes == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return (file_attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool
create_directory_if_not_exists(const std::string &path) {
  if (!directory_exists(path)) {
    std::string fixed_path = path;
    std::replace(fixed_path.begin(), fixed_path.end(), '/', '\\');  // Windows 경로 변환
    size_t pos = fixed_path.find_last_of("\\");
    if (pos != std::string::npos) {
      std::string parent_path = fixed_path.substr(0, pos);
      if (create_directory_if_not_exists(parent_path)) {
        return CreateDirectoryA(fixed_path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
      }
    }
    std::cerr << "Failed to create directory: " << path << ". Error: " << GetLastError() << std::endl;
    return false;
  }
  return true;
}

bool
replace_all(const std::string &source_dir, const std::string &dest_dir) {
  WIN32_FIND_DATA find_file_data;
  HANDLE hFind = FindFirstFile((source_dir + "\\*").c_str(), &find_file_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    std::cerr << "Failed to find first file in directory: " << source_dir << std::endl;
    return false;
  }

  // Ensure the destination directory exists
  if (!create_directory_if_not_exists(dest_dir)) {
    FindClose(hFind);
    return false;
  }

  do {
    const std::string file_name = find_file_data.cFileName;
    if (file_name == "." || file_name == ".." || file_name == "updater.exe") {
      continue;
    }

    const std::string source_path = source_dir + "\\" + file_name;
    const std::string dest_path = dest_dir + "\\" + file_name;

    if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Recursively move subdirectories
      if (!replace_all(source_path, dest_path)) {
        FindClose(hFind);
        return false;
      }
    }
    else {
      // Move files
      if (!MoveFileEx(source_path.c_str(), dest_path.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        std::cerr << "FileName : " << file_name << std::endl;
        std::cerr << "Failed to move file: " << source_path << " to " << dest_path << ". Error: " << GetLastError() << std::endl;
        continue;
        /*FindClose(hFind);
        return false;*/
      }
    }
  } while (FindNextFile(hFind, &find_file_data) != 0);

  FindClose(hFind);
  return true;
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
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != (DWORD) lParam) {
      return TRUE;
    }

    // 최상위 보이는 창만 대상으로 함 (소유 창 제외)
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
      return TRUE;
    }

    // 작업표시줄/Alt+Tab에서 숨기기: APPWINDOW 제거, TOOLWINDOW 추가
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    LONG_PTR newEx = ex;
    newEx &= ~WS_EX_APPWINDOW;
    newEx |= WS_EX_TOOLWINDOW;
    if (newEx != ex) {
      SetWindowLongPtr(hwnd, GWL_EXSTYLE, newEx);
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    // 화면에서 숨기기
    ShowWindowAsync(hwnd, SW_HIDE);
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

int
main(int argc, char *argv[]) {
  std::string process_name = "WmCLt.exe";
  std::ofstream log_file(get_self_path() +"\\update.log");
  if (!log_file.is_open()) {
    std::cerr << "Failed to open log file." << std::endl;
    return 1;
  }

  std::streambuf *cout_buf = std::cout.rdbuf();
  std::streambuf *cerr_buf = std::cerr.rdbuf();
  std::cout.rdbuf(log_file.rdbuf());
  std::cerr.rdbuf(log_file.rdbuf());
  // 업데이트 경로
  std::cout << "process_name - " << process_name << std::endl;

  // 대상 프로세스가 실행되는 동안 주기적으로 숨김 처리
  while (true) {
    DWORD pid = GetProcessIdByName(process_name);
    if (pid) {
      EnumWindows(EnumWindowsProc, (LPARAM)pid);
    }
    Sleep(300); // 0.3초 주기
  }

  // 아래는 루프가 종료될 때만 실행됨
  std::cout.rdbuf(cout_buf);
  std::cerr.rdbuf(cerr_buf);
  log_file.close();

  return 0;
}
