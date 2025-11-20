# windows specific dependencies

# nlohmann_json
pkg_check_modules(NLOHMANN_JSON nlohmann_json REQUIRED IMPORTED_TARGET)

# libssh for SSH server functionality
find_package(libssh)
if(libssh_FOUND)
    message(STATUS "Found libssh: ${libssh_VERSION}")
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES ssh)
else()
    message(WARNING "libssh not found. SSH server features will be disabled. Install libssh to enable SSH tunneling for USB/IP.")
    list(APPEND SUNSHINE_DEFINITIONS DISABLE_SSH_SERVER)
endif()
