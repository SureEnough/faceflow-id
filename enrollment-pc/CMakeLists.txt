cmake_minimum_required(VERSION 3.16)
project(enrollment_pc LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------- Qt 应用（可选） ----------------
# 依赖：Qt6（Widgets/Network/Multimedia）。未安装 Qt 时仅构建纯 C++ 逻辑自测。
find_package(Qt6 QUIET COMPONENTS Widgets Network Multimedia)

if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  set(CMAKE_AUTOUIC ON)

  add_executable(enrollment_app
    src/main.cpp
    src/api/backend_client.cpp
    src/capture/camera_view.cpp
    src/idcard/idcard_simulator.cpp
    src/ui/MainWindow.cpp
    src/verify/verify_logic.cpp
  )
  target_include_directories(enrollment_app PRIVATE src)
  target_link_libraries(enrollment_app PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
  )
else()
  message(STATUS "Qt6 not found - enrollment_app skipped (install qt6-base-dev / qt6-multimedia-dev to build)")
endif()

# ---------------- 纯 C++ 逻辑自测（无 Qt 依赖） ----------------
enable_testing()
add_executable(enroll_logic_test tests/logic_test.cpp
  src/idcard/idcard_simulator.cpp
  src/verify/verify_logic.cpp
)
target_include_directories(enroll_logic_test PRIVATE src)
add_test(NAME logic COMMAND enroll_logic_test)