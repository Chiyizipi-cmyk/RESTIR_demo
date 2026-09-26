cmake_minimum_required(VERSION 3.20)
project(restir_gi LANGUAGES C CXX)

# ---------------------------------------------------------------
# ReSTIR GI 离线渲染 Demo（课题 A6）
# 一键构建：
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
# 产物：
#   build/restir-gi(.exe)   渲染器
#   build/restir_tests(.exe) 单元测试
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 预留的 Embree 升级路径（见 docs/00-selection.md §2）
option(USE_EMBREE "Use Intel Embree for ray intersection (requires embree installation)" OFF)
if(USE_EMBREE)
  find_package(embree 4 REQUIRED)
  add_compile_definitions(RESTIR_USE_EMBREE)
endif()

# 确定性要求：不启用 -ffast-math 等会破坏浮点一致性的优化
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
elseif(MSVC)
  add_compile_options(/W4 /wd4100)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

include_directories(
  ${CMAKE_SOURCE_DIR}/third_party/glm
  ${CMAKE_SOURCE_DIR}/third_party/tinyexr
  ${CMAKE_SOURCE_DIR}/third_party/stb
  ${CMAKE_SOURCE_DIR}/src
)

# 渲染核心库（渲染器 + 场景 + BVH + ReSTIR + IO）
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/core/*.h
  src/scene/*.cpp src/scene/*.h
  src/renderer/*.cpp src/renderer/*.h
  src/restir/*.cpp src/restir/*.h
  src/io/*.cpp src/io/*.h
)

add_library(restir_core STATIC ${CORE_SOURCES} third_party/tinyexr/miniz.c)
if(USE_EMBREE)
  target_link_libraries(restir_core PUBLIC embree)
endif()

add_executable(restir-gi src/main.cpp)
target_link_libraries(restir-gi PRIVATE restir_core)

# ---------------- 单元测试（自研断言框架，见 tests/test_framework.h） ----------------
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(restir_tests ${TEST_SOURCES})
target_link_libraries(restir_tests PRIVATE restir_core)
add_test(NAME unit COMMAND restir_tests)
