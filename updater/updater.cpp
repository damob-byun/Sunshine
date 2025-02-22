#include "minizip-ng/mz.h"
#include "minizip-ng/mz_strm.h"
#include "minizip-ng/mz_zip.h"
#include "minizip-ng/mz_zip_rw.h"
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

std::string
calculate_sha256(const std::string &filePath) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open file for SHA256 calculation: " << filePath << std::endl;
    return "";
  }

  SHA256_CTX sha256;
  SHA256_Init(&sha256);

  char buffer[8192];
  while (file.good()) {
    file.read(buffer, sizeof(buffer));
    SHA256_Update(&sha256, buffer, file.gcount());
  }

  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_Final(hash, &sha256);

  std::ostringstream oss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << (int) hash[i];
  }

  return oss.str();
}

bool
extract_zip_with_powerShell(const std::string &zipPath, const std::string &outputPath) {
  // PowerShell 명령 생성
  std::string command = "powershell -Command \"Expand-Archive -LiteralPath '" + zipPath + "' -DestinationPath '" + outputPath + "' -Force\"";

  // PowerShell 명령 실행
  int result = std::system(command.c_str());

  // 결과 확인
  if (result == 0) {
    std::cout << "Successfully extracted ZIP file to: " << outputPath << std::endl;
    return true;
  }
  else {
    std::cerr << "Failed to extract ZIP file. PowerShell returned code: " << result << std::endl;
    return false;
  }
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
void
relaunch_program(const std::string &programPath) {
#ifdef _WIN32
  // Windows에서 프로그램 재실행
  STARTUPINFO si = { sizeof(STARTUPINFO) };
  PROCESS_INFORMATION pi;
  if (!CreateProcess(NULL, const_cast<char *>(programPath.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    std::cerr << "Failed to relaunch program. Error: " << GetLastError() << std::endl;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
#else
  // Unix-like 시스템에서 프로그램 재실행
  if (fork() == 0) {
    execl(programPath.c_str(), programPath.c_str(), NULL);
    std::cerr << "Failed to relaunch program." << std::endl;
    exit(EXIT_FAILURE);
  }
#endif
}

void
ensure_directory_exists(const std::string &file_path) {
  size_t pos = file_path.find_last_of("\\");
  if (pos != std::string::npos) {
    std::string directory = file_path.substr(0, pos);
    create_directory_if_not_exists(directory);
  }
}

bool
extract_zip(const std::string &zip_file_path, const std::string &output_folder) {
  std::string final_output_path = fix_path_for_windows(output_folder);

  if (!create_directory_if_not_exists(final_output_path)) {
    return false;
  }

  void *zip_reader = mz_zip_reader_create();
  if (mz_zip_reader_open_file(zip_reader, zip_file_path.c_str()) != MZ_OK) {
    std::cerr << "Failed to open ZIP file: " << zip_file_path << std::endl;
    mz_zip_reader_delete(&zip_reader);
    return false;
  }

  mz_zip_file *file_info = nullptr;
  while (mz_zip_reader_goto_next_entry(zip_reader) == MZ_OK) {
    mz_zip_reader_entry_get_info(zip_reader, &file_info);
    std::string filename = fix_path_for_windows(file_info->filename);
    std::string full_path = final_output_path + "\\" + filename;

    // std::cout << "Extracting: " << full_path << std::endl;

    if (filename.back() == '\\') {
      create_directory_if_not_exists(full_path);
      continue;
    }

    if (mz_zip_reader_entry_open(zip_reader) != MZ_OK) {
      std::cerr << "Failed to open file inside ZIP archive." << std::endl;
      continue;
    }

    ensure_directory_exists(full_path);
    std::ofstream out_file(full_path, std::ios::binary);
    if (!out_file) {
      std::cerr << "Failed to create output file: " << full_path << std::endl;
      mz_zip_reader_entry_close(zip_reader);
      continue;
    }

    std::vector<char> buffer(8192);
    int32_t bytes_read;
    while ((bytes_read = mz_zip_reader_entry_read(zip_reader, buffer.data(), buffer.size())) > 0) {
      out_file.write(buffer.data(), bytes_read);
    }

    out_file.close();
    mz_zip_reader_entry_close(zip_reader);
  }

  mz_zip_reader_delete(&zip_reader);
  std::cout << "Extraction completed: " << final_output_path << std::endl;
  return true;
}

int
main(int argc, char *argv[]) {
  std::string program_path;
  if (argc >= 2) {
    program_path = argv[1];
  }
  else {
    program_path = get_self_path() + "\\Sunshine.exe";
  }
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
  std::cout << "program_path - " << program_path << std::endl;

  const std::string temp_update_file = get_self_path() + "\\update.zip";  // 다운로드한 임시 업데이트 파일
  const std::string temp_checksum_file = get_self_path() + "\\update.zip.sha256";
  std::cout << "Starting update process..." << std::endl;

  
  // TODO: 무결성체크
  const std::string update_url = "https://forpro.co.kr/updates/Sunshine.zip";
  const std::string checksum_url = "https://forpro.co.kr/updates/Sunshine.zip.sha256";
  std::cout << "Downloading update from: " << update_url << std::endl;
  if (!download_file(update_url, temp_update_file)) {
    std::cerr << "Failed to download update file." << std::endl;
    return 1;
  }
  std::cout << "Download completed successfully." << std::endl;

  if (download_file(checksum_url, temp_checksum_file)) {
    std::string checksum = calculate_sha256(temp_update_file);
    std::ifstream checksum_file(temp_checksum_file);
    std::string expected_checksum;
    checksum_file >> expected_checksum;
    if (checksum != expected_checksum) {
      std::cerr << "Checksum mismatch. Expected: " << expected_checksum << ", got: " << checksum << std::endl;
      return 1;
    }
  }
  else {
    std::cerr << "Failed to download checksum file." << std::endl;
    return 1;
  }

  bool stop_service_result = stop_service("SunshineService");
  if (!stop_service_result) {
    std::cerr << "Failed to stop service." << std::endl;
    if (!kill_process_by_name("sunshine.exe")) {
      std::cerr << "Failed to terminate sunshine.exe if it was running." << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));

  }
  else {
    std::cout << "Service stopped successfully." << std::endl;\
      // 업데이트 전 대기 (프로그램 종료 대기 시간)
    std::this_thread::sleep_for(std::chrono::seconds(12));

  }

  
  

  /*if (!kill_process_by_name("sunshinesvc.exe")) {
    std::cerr << "Failed to terminate sunshinesvc.exe if it was running." << std::endl;
  }*/

  std::cout << "Extracting update files..." << std::endl;
  if (!extract_zip(temp_update_file, get_parent_directory(program_path))) {
    std::cerr << "Failed to extract update files." << std::endl;
    return 1;
  }

  const std::string extracted_path = get_parent_directory(program_path) + "\\Sunshine";
  if (!replace_all(extracted_path, get_parent_directory(program_path))) {
    std::cerr << "Failed to replace program file." << std::endl;
    return 1;
  }
  /*const std::string src_sunshine_path = get_parent_directory(program_path) + "\\Sunshine\\sunshine.exe";
  const std::string dest_sunshine_path = get_parent_directory(program_path) + "\\sunshine.exe";

  if (!MoveFileEx(src_sunshine_path.c_str(), dest_sunshine_path.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
    std::cerr << "Failed to move file: " << src_sunshine_path << " to " << dest_sunshine_path << ". Error: " << GetLastError() << std::endl;
  }*/

  // delete extracted_path
  fs::remove_all(extracted_path);

  std::cout << "Update completed successfully." << std::endl;
  std::cout << "Update successful. Restarting program..." << std::endl;

  // 프로그램 재실행
  if (stop_service_result) {
    bool ret = start_service("SunshineService");
    if (!ret) {
      std::cerr << "Failed to start service." << std::endl;
    }
    else {
      std::cout << "Service started successfully." << std::endl;
    }
  }
  else {
    relaunch_program(program_path);
  }

  std::cout.rdbuf(cout_buf);
  std::cerr.rdbuf(cerr_buf);
  log_file.close();

  return 0;
}
