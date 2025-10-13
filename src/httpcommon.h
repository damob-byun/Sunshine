/**
 * @file src/httpcommon.h
 * @brief Declarations for common HTTP.
 */
#pragma once

#include "network.h"
#include "thread_safe.h"

namespace http {

  int
  init();
  int
  create_creds(const std::string &pkey, const std::string &cert);
  int
  save_user_creds(
    const std::string &file,
    const std::string &username,
    const std::string &password,
    bool run_our_mouth = false);

  int
  reload_user_creds(const std::string &file);
  bool
  download_file(const std::string &url, const std::string &file);
  std::string
  url_escape(const std::string &url);
  std::string
  url_get_host(const std::string &url);
  std::vector<std::string>
  get_available_ips();
  bool
  check_whitelist_ip(const std::string& ip, const std::string *auth_header = nullptr);

  std::string
  check_pair_and_get_pin(const std::string& ip, const std::string *auth_header = nullptr);


  std::string
  get_public_ip();

  std::string
  get_latest_version();

  bool
  update_is_alive();

  void
  startTimer(boost::asio::steady_timer& timers);

  extern std::string unique_id;
  extern net::net_e origin_web_ui_allowed;

  const std::string API_HOST = "https://remote-pc.co.kr"; 

}  // namespace http
