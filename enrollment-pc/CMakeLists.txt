cmake_minimum_required(VERSION 3.16)
project(enrollment_pc LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------- 特征提取模块（纯 C++，无 Qt / 无 ONNX 亦可编译） ----------------
set(ENROLL_FEATURE_SOURCES
  src/feature/align.cpp
  src/feature/mock_feature_extractor.cpp
  src/feature/ort_feature_extractor.cpp
  src/feature/feature_extractor_factory.cpp
)

# ONNX Runtime（特征提取真实现；缺失时自动回退 Mock，测试仍可全绿）
find_package(onnxruntime QUIET)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h)
find_library(ONNXRUNTIME_LIB onnxruntime)

# 对目标应用 ONNX 宏/头/库（仅当找到 ORT 时定义 HAVE_ONNXRUNTIME）
macro(apply_onnx TGT)
  if(ONNXRUNTIME_LIB)
    target_compile_definitions(${TGT} PRIVATE HAVE_ONNXRUNTIME=1)
    target_include_directories(${TGT} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(${TGT} PRIVATE ${ONNXRUNTIME_LIB})
  endif()
endmacro()

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
    ${ENROLL_FEATURE_SOURCES}
  )
  target_include_directories(enrollment_app PRIVATE src)
  target_link_libraries(enrollment_app PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
  )
  apply_onnx(enrollment_app)
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

add_executable(enroll_feature_test tests/feature_test.cpp
  ${ENROLL_FEATURE_SOURCES}
  src/verify/verify_logic.cpp
)
target_include_directories(enroll_feature_test PRIVATE src)
apply_onnx(enroll_feature_test)
add_test(NAME feature COMMAND enroll_feature_test)