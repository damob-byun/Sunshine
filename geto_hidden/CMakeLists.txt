cmake_minimum_required(VERSION 3.10)

# 프로젝트 이름 및 버전 설정
project(sunshine-updater VERSION 1.0)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 실행 파일 이름 설정
set(EXECUTABLE_NAME updater)

# 소스 파일 추가
set(SOURCES
    updater.cpp # 프로젝트의 메인 소스 파일
)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)


# libcurl 정적 링크 설정
set(OPENSSL_LIBRARIES
        libssl.a
        libcrypto.a)
        
list(PREPEND PLATFORM_LIBRARIES
    libstdc++.a
    libwinpthread.a
    libssp.a
    ntdll
    ksuser
    ws2_32
    wsock32
    iphlpapi
    ${CMAKE_SOURCE_DIR}/minizip-ng/build/libminizip.a
    ${ZLIB_LIBRARIES}
    -lbz2
    -llzma
    ${CURL_STATIC_LIBRARIES})

# curl
add_definitions(-DCURL_STATICLIB)
include_directories(SYSTEM ${CURL_STATIC_INCLUDE_DIRS})
link_directories(${CURL_STATIC_LIBRARY_DIRS})


set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
find_library(ZLIB ZLIB1)

# 실행 파일 생성
add_executable(${EXECUTABLE_NAME} ${SOURCES})


# 실행 파일에 라이브러리 연결
target_link_libraries(${EXECUTABLE_NAME}
    ${OPENSSL_LIBRARIES}
    ${PLATFORM_LIBRARIES}
)

set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_SEARCH_START_STATIC 1)