# Updater

mingw ucrt64

```bash
pacman -S mingw-w64-ucrt-x86_64-windows-default-manifest mingw-w64-ucrt-x86_64-headers mingw-w64-ucrt-x86_64-crt
dependencies=(
  "git"
  "mingw-w64-ucrt-x86_64-boost"  # Optional
  "mingw-w64-ucrt-x86_64-cmake"
  "mingw-w64-ucrt-x86_64-cppwinrt"
  "mingw-w64-ucrt-x86_64-curl-winssl"
  "mingw-w64-ucrt-x86_64-doxygen"  # Optional, for docs... better to install official Doxygen
  "mingw-w64-ucrt-x86_64-graphviz"  # Optional, for docs
  "mingw-w64-ucrt-x86_64-MinHook"
  "mingw-w64-ucrt-x86_64-miniupnpc"
  "mingw-w64-ucrt-x86_64-nlohmann-json"
  "mingw-w64-ucrt-x86_64-nodejs"
  "mingw-w64-ucrt-x86_64-nsis"
  "mingw-w64-ucrt-x86_64-onevpl"
  "mingw-w64-ucrt-x86_64-openssl"
  "mingw-w64-ucrt-x86_64-opus"
  "mingw-w64-ucrt-x86_64-toolchain"
)
pacman -S "${dependencies[@]}"
```

### Updater Build

```bash
git pull --recurse-submodules
cd updater
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
```

### Sunshine Build

```bash
mkdir build
cmake -B build -G Ninja -S .
ninja -C build
```

### Sunshine Package

```bash
cd build
cpack -G NSIS --config ./CPackConfig.cmake
cpack -G ZIP --config ./CPackConfig.cmake
```


