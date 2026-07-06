# native/CMakeLists.txt
#
# 双用途：
#   - macOS/桌面：构建 bench 可执行文件（本地实测 ≤300ms + 对拍）。
#   - Android NDK：Flutter externalNativeBuild 引用本文件，构建 libhistogram.so
#     供 dart:ffi 加载（histogram 库目标平台无关，直接复用）。
cmake_minimum_required(VERSION 3.16)
project(histogram LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 性能优化开关；arm64（macOS Apple Silicon / iPhone / Android arm64）自动启用 NEON。
# -ffp-contract=off：禁用 FMA 融合，保证灰度公式结果跨平台确定一致（准确性口径）。
add_compile_options(-O3 -ffp-contract=off)

# 共享计算核心库（FFI 与 bench 共用同一份源码）。
add_library(histogram SHARED histogram.cpp)
set_target_properties(histogram PROPERTIES
  PUBLIC_HEADER histogram.h
  # iOS/Android FFI 需保留导出符号，避免被 strip。
  C_VISIBILITY_PRESET default
  CXX_VISIBILITY_PRESET default)

find_package(Threads REQUIRED)
target_link_libraries(histogram PRIVATE Threads::Threads)

# 仅桌面构建 bench（Android NDK 构建时无需，用 ANDROID 变量判定）。
if(NOT ANDROID AND NOT IOS)
  add_executable(bench bench/bench.cpp histogram.cpp)
  target_link_libraries(bench PRIVATE Threads::Threads)
endif()
