# Updater

mingw ucrt64


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
cpack -G NSIS --config ./CPackConfig.cmake
cpack -G ZIP --config ./CPackConfig.cmake
```
