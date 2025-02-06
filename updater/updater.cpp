#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <openssl/sha.h> 

//#include <minizip/unzip.h>
#ifdef _WIN32
  #include <windows.h>
#endif



namespace fs = std::filesystem;

bool kill_process_by_name(const std::string& process_name) {
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE) {
    std::cerr << "Failed to create snapshot of processes." << std::endl;
    return false;
  }

  PROCESSENTRY32 pe;
  pe.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(hSnapshot, &pe)) {
    do {
      if (process_name == pe.szExeFile) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
        if (hProcess == NULL) {
          std::cerr << "Failed to open process for termination: " << process_name << std::endl;
          CloseHandle(hSnapshot);
          return false;
        }

        if (!TerminateProcess(hProcess, 0)) {
          std::cerr << "Failed to terminate process: " << process_name << std::endl;
          CloseHandle(hProcess);
          CloseHandle(hSnapshot);
          return false;
        }

        CloseHandle(hProcess);
        std::cout << "Successfully terminated process: " << process_name << std::endl;
        CloseHandle(hSnapshot);
        return true;
      }
    } while (Process32Next(hSnapshot, &pe));
  }

  std::cerr << "Process not found: " << process_name << std::endl;
  CloseHandle(hSnapshot);
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
    std::cerr << "Couldn't create directory "<< file_dir << std::endl;
    curl_easy_cleanup(curl);
    return false;
  }

  FILE *fp = fopen(file.c_str(), "wb");
  if (!fp) {
    std::cerr << "Couldn't open "<< file << std::endl;
    curl_easy_cleanup(curl);
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

  CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    std::cerr << "Couldn't download "<< url << ", code:" << result << std::endl;
  }

  curl_easy_cleanup(curl);
  fclose(fp);
  return result == CURLE_OK;
}

std::string calculate_sha256(const std::string& filePath) {
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
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return oss.str();
}

bool extract_zip_with_powerShell(const std::string& zipPath, const std::string& outputPath) {
    // PowerShell 명령 생성
    std::string command = "powershell -Command \"Expand-Archive -LiteralPath '"
                          + zipPath + "' -DestinationPath '"
                          + outputPath + "' -Force\"";

    // PowerShell 명령 실행
    int result = std::system(command.c_str());

    // 결과 확인
    if (result == 0) {
        std::cout << "Successfully extracted ZIP file to: " << outputPath << std::endl;
        return true;
    } else {
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

bool directory_exists(const std::string& path) {
  DWORD file_attributes = GetFileAttributes(path.c_str());
  if (file_attributes == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return (file_attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool create_directory_if_not_exists(const std::string& path) {
  if (!directory_exists(path)) {
    if (!CreateDirectory(path.c_str(), NULL)) {
      if (GetLastError() != ERROR_ALREADY_EXISTS) {
        std::cerr << "Failed to create directory: " << path << ". Error: " << GetLastError() << std::endl;
        return false;
      }
    }
  }
  return true;
}


bool replace_all(const std::string& source_dir, const std::string& dest_dir) {
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
    if (file_name == "." || file_name == "..") {
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
    } else {
      // Move files
      if (!MoveFileEx(source_path.c_str(), dest_path.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        std::cerr << "Failed to move file: " << source_path << " to " << dest_path << ". Error: " << GetLastError() << std::endl;
        FindClose(hFind);
        return false;
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

int
main(int argc, char *argv[]) {
  std::string program_path;  
  if (argc >= 2) {
    program_path = argv[1];
  }else{
    program_path = get_self_path() + "\\Sunshine.exe";
  }

  // 업데이트 경로
  std::cout << "program_path - " << program_path << std::endl;

  const std::string temp_update_file = get_self_path() + "\\update.zip";  // 다운로드한 임시 업데이트 파일
  const std::string temp_checksum_file = get_self_path() + "\\update.zip.sha256"; 
  std::cout << "Starting update process..." << std::endl;

  // 업데이트 전 대기 (프로그램 종료 대기 시간)
  std::this_thread::sleep_for(std::chrono::seconds(2));

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


  std::cout << "Extracting update files..." << std::endl;
  if (!extract_zip_with_powerShell(temp_update_file, get_parent_directory(program_path))) {
    std::cerr << "Failed to extract update files." << std::endl;
    return 1;
  }

  if (!kill_process_by_name("Sunshine.exe")) {
    std::cerr << "Failed to terminate Sunshine.exe if it was running." << std::endl;
  }

  const std::string extracted_path = get_parent_directory(program_path) + "\\Sunshine";
  if (!replace_all(extracted_path, get_parent_directory(program_path))) {
    std::cerr << "Failed to replace program file." << std::endl;
    return 1;
  }


  std::cout << "Update completed successfully." << std::endl;
  std::cout << "Update successful. Restarting program..." << std::endl;

  // 프로그램 재실행
  relaunch_program(program_path);

  return 0;
}
