/**
 * @file src/httpcommon.cpp
 * @brief Definitions for common HTTP.
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>
#include <utility>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/asio/ssl/context.hpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl/context_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <curl/curl.h>

#include "config.h"
#include "crypto.h"
#include "file_handler.h"
#include "httpcommon.h"
#include "logging.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "rtsp.h"
#include "utility.h"
#include "uuid.h"

namespace http {
  using namespace std::literals;
  namespace fs = std::filesystem;
  namespace pt = boost::property_tree;

  int
  reload_user_creds(const std::string &file);
  bool
  user_creds_exist(const std::string &file);

  std::string unique_id;
  net::net_e origin_web_ui_allowed;
  std::string _my_public_ip;

  int
  init() {
    bool clean_slate = config::sunshine.flags[config::flag::FRESH_STATE];
    origin_web_ui_allowed = net::from_enum_string(config::nvhttp.origin_web_ui_allowed);

    if (clean_slate) {
      unique_id = uuid_util::uuid_t::generate().string();
      auto dir = std::filesystem::temp_directory_path() / "Sunshine"sv;
      config::nvhttp.cert = (dir / ("cert-"s + unique_id)).string();
      config::nvhttp.pkey = (dir / ("pkey-"s + unique_id)).string();
    }

    if (!fs::exists(config::nvhttp.pkey) || !fs::exists(config::nvhttp.cert)) {
      if (create_creds(config::nvhttp.pkey, config::nvhttp.cert)) {
        return -1;
      }
    }
    if (user_creds_exist(config::sunshine.credentials_file)) {
      if (reload_user_creds(config::sunshine.credentials_file)) return -1;
    }
    else {
      BOOST_LOG(info) << "Open the Web UI to set your new username and password and getting started";
    }
    return 0;
  }

  int
  save_user_creds(const std::string &file, const std::string &username, const std::string &password, bool run_our_mouth) {
    pt::ptree outputTree;

    if (fs::exists(file)) {
      try {
        pt::read_json(file, outputTree);
      }
      catch (std::exception &e) {
        BOOST_LOG(error) << "Couldn't read user credentials: "sv << e.what();
        return -1;
      }
    }

    auto salt = crypto::rand_alphabet(16);
    outputTree.put("username", username);
    outputTree.put("salt", salt);
    outputTree.put("password", util::hex(crypto::hash(password + salt)).to_string());
    try {
      pt::write_json(file, outputTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(error) << "error writing to the credentials file, perhaps try this again as an administrator? Details: "sv << e.what();
      return -1;
    }

    BOOST_LOG(info) << "New credentials have been created"sv;
    return 0;
  }

  bool
  user_creds_exist(const std::string &file) {
    if (!fs::exists(file)) {
      return false;
    }

    pt::ptree inputTree;
    try {
      pt::read_json(file, inputTree);
      return inputTree.find("username") != inputTree.not_found() &&
             inputTree.find("password") != inputTree.not_found() &&
             inputTree.find("salt") != inputTree.not_found();
    }
    catch (std::exception &e) {
      BOOST_LOG(error) << "validating user credentials: "sv << e.what();
    }

    return false;
  }

  int
  reload_user_creds(const std::string &file) {
    pt::ptree inputTree;
    try {
      pt::read_json(file, inputTree);
      config::sunshine.username = inputTree.get<std::string>("username");
      config::sunshine.password = inputTree.get<std::string>("password");
      config::sunshine.salt = inputTree.get<std::string>("salt");
    }
    catch (std::exception &e) {
      BOOST_LOG(error) << "loading user credentials: "sv << e.what();
      return -1;
    }
    return 0;
  }

  int
  create_creds(const std::string &pkey, const std::string &cert) {
    fs::path pkey_path = pkey;
    fs::path cert_path = cert;

    auto creds = crypto::gen_creds("Sunshine Gamestream Host"sv, 2048);

    auto pkey_dir = pkey_path;
    auto cert_dir = cert_path;
    pkey_dir.remove_filename();
    cert_dir.remove_filename();

    std::error_code err_code {};
    fs::create_directories(pkey_dir, err_code);
    if (err_code) {
      BOOST_LOG(error) << "Couldn't create directory ["sv << pkey_dir << "] :"sv << err_code.message();
      return -1;
    }

    fs::create_directories(cert_dir, err_code);
    if (err_code) {
      BOOST_LOG(error) << "Couldn't create directory ["sv << cert_dir << "] :"sv << err_code.message();
      return -1;
    }

    if (file_handler::write_file(pkey.c_str(), creds.pkey)) {
      BOOST_LOG(error) << "Couldn't open ["sv << config::nvhttp.pkey << ']';
      return -1;
    }

    if (file_handler::write_file(cert.c_str(), creds.x509)) {
      BOOST_LOG(error) << "Couldn't open ["sv << config::nvhttp.cert << ']';
      return -1;
    }

    fs::permissions(pkey_path,
      fs::perms::owner_read | fs::perms::owner_write,
      fs::perm_options::replace, err_code);

    if (err_code) {
      BOOST_LOG(error) << "Couldn't change permissions of ["sv << config::nvhttp.pkey << "] :"sv << err_code.message();
      return -1;
    }

    fs::permissions(cert_path,
      fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read | fs::perms::owner_write,
      fs::perm_options::replace, err_code);

    if (err_code) {
      BOOST_LOG(error) << "Couldn't change permissions of ["sv << config::nvhttp.cert << "] :"sv << err_code.message();
      return -1;
    }

    return 0;
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
      BOOST_LOG(error) << "Couldn't create CURL instance";
      return false;
    }

    std::string file_dir = file_handler::get_parent_directory(file);
    if (!file_handler::make_directory(file_dir)) {
      BOOST_LOG(error) << "Couldn't create directory ["sv << file_dir << ']';
      curl_easy_cleanup(curl);
      return false;
    }

    FILE *fp = fopen(file.c_str(), "wb");
    if (!fp) {
      BOOST_LOG(error) << "Couldn't open ["sv << file << ']';
      curl_easy_cleanup(curl);
      return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
      BOOST_LOG(error) << "Couldn't download ["sv << url << ", code:" << result << ']';
    }

    curl_easy_cleanup(curl);
    fclose(fp);
    return result == CURLE_OK;
  }

  std::string
  url_escape(const std::string &url) {
    CURL *curl = curl_easy_init();
    char *string = curl_easy_escape(curl, url.c_str(), url.length());
    std::string result(string);
    curl_free(string);
    curl_easy_cleanup(curl);
    return result;
  }

  std::string
  url_get_host(const std::string &url) {
    CURLU *curlu = curl_url();
    curl_url_set(curlu, CURLUPART_URL, url.c_str(), url.length());
    char *host;
    if (curl_url_get(curlu, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
      curl_url_cleanup(curlu);
      return "";
    }
    std::string result(host);
    curl_free(host);
    curl_url_cleanup(curlu);
    return result;
  }

  size_t
  m_write_callback(void *contents, size_t size, size_t nmemb, std::string *userData) {
    size_t totalSize = size * nmemb;
    userData->append((char *) contents, totalSize);
    return totalSize;
  }

  std::vector<std::string>
  get_available_ips() {
    CURL *curl;
    CURLcode res;
    std::string response;
    std::vector<std::string> lines;
    curl = curl_easy_init();
    if (curl) {
      // URL 설정
      std::string url = API_HOST + "/api/public/ip";
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

      // 콜백 함수 설정
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, m_write_callback);

      // 데이터를 저장할 버퍼 설정
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

      // curl_easy_setopt(curl, CURLOPT_CAINFO, "./cacert.pem");

      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Total timeout of 10 seconds
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // Connection timeout of 5 seconds

      // 요청 실행
      res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
      }
      else {
        // 응답 데이터를 \n으로 나눠서 벡터에 저장
        std::istringstream stream(response);
        std::string line;
        while (std::getline(stream, line)) {
          lines.push_back(line);
        }
      }

      // curl 정리
      curl_easy_cleanup(curl);
    }
    else {
      std::cerr << "Failed to initialize libcurl." << std::endl;
    }

    return lines;
  }

  bool
  check_whitelist_ip(const std::string &ip, const std::string *auth_header) {
    CURL *curl;
    CURLcode res;
    std::string response;
    std::vector<std::string> lines;
    curl = curl_easy_init();
    if (curl) {
      std::string url = API_HOST + "/api/public/is-white-list-ip?ip=" + ip;
      // URL 설정
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

      // Authorization 헤더 추가
      struct curl_slist *headers = NULL;
      if (auth_header && !auth_header->empty()) {
        std::string auth = "Authorization: " + *auth_header;
        headers = curl_slist_append(headers, auth.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      }

      // 콜백 함수 설정
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, m_write_callback);

      // 데이터를 저장할 버퍼 설정
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

      // curl_easy_setopt(curl, CURLOPT_CAINFO, "./cacert.pem");

      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

      // Set timeout options
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Total timeout of 10 seconds
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // Connection timeout of 5 seconds

      // 요청 실행
      res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return false;
      }
      else {
        if (response == "true") {
          return true;
        }
        else if (response == "false") {
          return false;
        }
        else {
          std::cerr << "Unexpected response: " << response << std::endl;
          return false;
        }
      }

      // curl 정리
      curl_easy_cleanup(curl);
    }
    else {
      std::cerr << "Failed to initialize libcurl." << std::endl;
    }

    return false;
  }

  std::string
  trim(const std::string &str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    return (start == std::string::npos || end == std::string::npos) ? "" : str.substr(start, end - start + 1);
  }

  std::string
  getPublicIP() {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    // Initialize curl
    curl = curl_easy_init();
    if (curl) {
      // Set URL to ifconfig.me
      curl_easy_setopt(curl, CURLOPT_URL, "https://checkip.amazonaws.com");

      // Set write function callback
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, m_write_callback);

      // Set the string buffer to write the response data
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Total timeout of 10 seconds
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // Connection timeout of 5 seconds

      // Perform the request
      res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
        std::cerr << "getPublicIP() failed: " << curl_easy_strerror(res) << std::endl;
        readBuffer.clear();  // Clear the buffer to indicate failure
      }

      // Clean up curl
      curl_easy_cleanup(curl);
    }
    else {
      std::cerr << "Failed to initialize curl" << std::endl;
    }

    return trim(readBuffer);
  }

  bool
  update_is_alive() {
    CURL *curl;
    CURLcode res;
    std::string response;
    std::vector<std::string> lines;
    curl = curl_easy_init();
    if (curl) {
      if (_my_public_ip.empty()) {
        _my_public_ip = getPublicIP();
        BOOST_LOG(info) << "my_public_ip: " + _my_public_ip << std::endl;
      }

      std::string url = API_HOST + "/api/public/alive?ip=" + _my_public_ip;
      // BOOST_LOG(info) << url << std::endl;
      //  URL 설정
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

      // 콜백 함수 설정
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, m_write_callback);

      // 데이터를 저장할 버퍼 설정
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

      // curl_easy_setopt(curl, CURLOPT_CAINFO, "./cacert.pem");

      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Total timeout of 10 seconds
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // Connection timeout of 5 seconds

      // 요청 실행
      res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
        std::cerr << "update_is_alive() failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return false;
      }
      else {
        BOOST_LOG(info) << "update_is_alive: " << response << std::endl;
        if (response == "true") {
          return true;
        }
        else if (response == "false") {
          return false;
        }
        else {
          std::cerr << "Unexpected response: " << response << std::endl;
          return false;
        }
      }

      // curl 정리
      curl_easy_cleanup(curl);
    }
    else {
      std::cerr << "Failed to initialize libcurl." << std::endl;
    }

    return false;
  }

  void
  startTimer(boost::asio::steady_timer &timer) {
    timer.expires_after(std::chrono::seconds(60));
    timer.async_wait([&timer](const boost::system::error_code &ec) {
      if (!ec) {
        update_is_alive();
        startTimer(timer);
      }
      else {
        std::cerr << "Timer error: " << ec.message() << std::endl;
      }
    });
  }


  std::string
  get_latest_version() {
    CURL *curl;
    CURLcode res;
    std::string response;
    std::vector<std::string> lines;
    curl = curl_easy_init();
    if (curl) {
      std::string url = API_HOST + "/api/public/sunshine-version";
      // BOOST_LOG(info) << url << std::endl;
      //  URL 설정
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

      // 콜백 함수 설정
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, m_write_callback);

      // 데이터를 저장할 버퍼 설정
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

      // curl_easy_setopt(curl, CURLOPT_CAINFO, "./cacert.pem");

      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Total timeout of 10 seconds
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // Connection timeout of 5 seconds

      // 요청 실행
      res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
        std::cerr << "get_latest_version() failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return "";
      }
      else {
        //BOOST_LOG(info) << "latest_version: " << response << std::endl;
        return response;
      }

      // curl 정리
      curl_easy_cleanup(curl);
    }
    else {
      std::cerr << "Failed to initialize libcurl." << std::endl;
    }

    return response;
  }
}  // namespace http
