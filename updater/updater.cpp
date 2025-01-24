#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <openssl/sha.h> 

//#include <minizip/unzip.h>
#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;

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


// ZIP 파일 압축 해제 함수 (파일 덮어쓰기 포함)
/*bool
extract_zip_and_replace(const std::string &zipPath) {
  unzFile zipFile = unzOpen(zipPath.c_str());
  if (!zipFile) {
    std::cerr << "Failed to open ZIP file: " << zipPath << std::endl;
    return false;
  }

  if (unzGoToFirstFile(zipFile) != UNZ_OK) {
    std::cerr << "Failed to read first file in ZIP archive." << std::endl;
    unzClose(zipFile);
    return false;
  }

  do {
    char fileName[256];
    unz_file_info fileInfo;
    if (unzGetCurrentFileInfo(zipFile, &fileInfo, fileName, sizeof(fileName), NULL, 0, NULL, 0) != UNZ_OK) {
      std::cerr << "Failed to get file info from ZIP archive." << std::endl;
      unzClose(zipFile);
      return false;
    }

    std::string filePath = fileName;

    // 디렉토리인지 파일인지 확인
    if (fileName[fileInfo.size_filename - 1] == '/') {
      // 디렉토리인 경우 생성
      fs::create_directories(filePath);
    }
    else {
      // 파일인 경우
      if (unzOpenCurrentFile(zipFile) != UNZ_OK) {
        std::cerr << "Failed to open file in ZIP archive: " << fileName << std::endl;
        unzClose(zipFile);
        return false;
      }

      std::ofstream outFile(filePath, std::ios::binary);
      if (!outFile.is_open()) {
        std::cerr << "Failed to create file: " << filePath << std::endl;
        unzCloseCurrentFile(zipFile);
        unzClose(zipFile);
        return false;
      }

      char buffer[8192];
      int bytesRead;
      while ((bytesRead = unzReadCurrentFile(zipFile, buffer, sizeof(buffer))) > 0) {
        outFile.write(buffer, bytesRead);
      }

      outFile.close();
      unzCloseCurrentFile(zipFile);

      if (bytesRead < 0) {
        std::cerr << "Error reading file from ZIP archive: " << fileName << std::endl;
        unzClose(zipFile);
        return false;
      }
    }
  } while (unzGoToNextFile(zipFile) == UNZ_OK);

  unzClose(zipFile);
  return true;
}*/

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
  if (argc < 2) {
    std::cerr << "Usage: updater <program_to_update>" << std::endl;
    return 1;
  }

  const std::string program_path = argv[1];  // 업데이트 경로
  const std::string temp_update_file = "update.zip";  // 다운로드한 임시 업데이트 파일
  const std::string temp_checksum_file = "update.zip.sha256"; 
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
  /*if (!extract_zip_and_replace(temp_update_file)) {
    std::cerr << "Failed to extract update files." << std::endl;
    return 1;
  }
  std::cout << "Update completed successfully." << std::endl;
  std::cout << "Update successful. Restarting program..." << std::endl;*/

  // 프로그램 재실행
  relaunch_program(program_path);

  std::cin.get();

  return 0;
}
