/**
 * @file src/virtual_display.h
 * @brief virtual_display.h is a header file for the virtual display.
 */


#include <string>

namespace virtual_display {
  bool
  isMonitorActive();
  bool
  change_resolution(int width, int height, int refreshRate);
  bool
  check_resolution();
  std::string
  execute_inline_powerShell(const std::string &command);
  bool 
  toggle_virtual_display(bool enable);
  bool
  exist_virtual_display();
     
}  // namespace virtual_display
