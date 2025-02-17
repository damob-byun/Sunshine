/**
 * @file file_handler.cpp
 * @brief Definitions for file handling functions.
 */

// standard includes
#include <filesystem>
#include <fstream>

// local includes
#include "file_handler.h"
#include "logging.h"

#ifdef _WIN32
    #include <windows.h>
#elif __APPLE__
    #include <mach-o/dyld.h>
#endif
namespace file_handler {
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
  char buffer[1024];

    #ifdef _WIN32
        GetModuleFileNameA(NULL, buffer, sizeof(buffer));
    #elif __APPLE__
        uint32_t size = sizeof(buffer);
        _NSGetExecutablePath(buffer, &size);
    #else
        return std::filesystem::canonical("/proc/self/exe");
    #endif
    return get_parent_directory(std::string(buffer));
  
}

  bool
  make_directory(const std::string &path) {
    // first, check if the directory already exists
    if (std::filesystem::exists(path)) {
      return true;
    }

    return std::filesystem::create_directories(path);
  }

  std::string
  read_file(const char *path) {
    if (!std::filesystem::exists(path)) {
      BOOST_LOG(debug) << "Missing file: " << path;
      return {};
    }

    std::ifstream in(path);
    return std::string { (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>() };
  }

  int
  write_file(const char *path, const std::string_view &contents) {
    std::ofstream out(path);

    if (!out.is_open()) {
      return -1;
    }

    out << contents;

    return 0;
  }
}  // namespace file_handler
