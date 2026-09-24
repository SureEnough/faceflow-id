cmake_minimum_required(VERSION 3.16)
project(edge_box LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EDGE_BOX_WITH_MOCK "Build with mock backend (no third-party deps, for CI/smoke)" ON)
option(EDGE_BOX_WITH_OPENCV "Use OpenCV for real video capture" ON)
option(EDGE_BOX_WITH_ONNX   "Use ONNX Runtime inference backend" ON)
option(EDGE_BOX_WITH_SQLITE "Use SQLite local store" ON)
option(EDGE_BOX_WITH_HTTP   "Use cpp-httplib reporter" ON)

# ---------------- 依赖探测 ----------------
# OpenCV（真实视频采集）
if(EDGE_BOX_WITH_OPENCV)
  find_package(OpenCV QUIET COMPONENTS core videoio imgproc imgcodecs)
endif()

# ONNX Runtime（推理后端）
if(EDGE_BOX_WITH_ONNX)
  find_package(onnxruntime QUIET)
  find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h)
  find_library(ONNXRUNTIME_LIB onnxruntime)
endif()

# SQLite3
if(EDGE_BOX_WITH_SQLITE)
  find_package(SQLite3 QUIET)
endif()

# cpp-httplib（上报，可直接把 httplib.h 放入 third_party）
find_path(CPPHTTPLIB_INCLUDE_DIR httplib.h HINTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# 共享源码（主程序 + 自测共用）
set(EDGE_BOX_SHARED_SOURCES
  src/backend/backend_factory.cpp
  src/backend/mock_backend.cpp
  src/backend/onnx_backend.cpp
  src/backend/scrfd_decode.cpp
  src/common/base64.cpp
  src/common/image_codec.cpp
  src/config/config.cpp
  src/config/mini_json.cpp
  src/face/face_align.cpp
  src/face/face_engine.cpp
  src/video/video_source.cpp
  src/pipeline/pipeline.cpp
  src/tracker/iou_tracker.cpp
  src/web/config_manager.cpp
  src/web/preview_store.cpp
  src/web/web_server.cpp
  src/flow/flow_counter.cpp
  src/recognizer/recognizer.cpp
  src/store/recognition_store.cpp
  src/report/api_client.cpp
  src/report/device_ops.cpp
  src/report/features_sync.cpp
  src/report/report_client.cpp
)

# ---------------- 目标 ----------------
set(EDGE_BOX_SOURCES
  src/main.cpp
  ${EDGE_BOX_SHARED_SOURCES}
)

add_executable(edge_box ${EDGE_BOX_SOURCES})
target_include_directories(edge_box PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# 依赖宏与链接（主程序 + 自测一致）
macro(apply_deps TGT)
  target_compile_definitions(${TGT} PRIVATE EDGE_BOX_VERSION="${PROJECT_VERSION}")
  if(OpenCV_FOUND)
    target_compile_definitions(${TGT} PRIVATE HAVE_OPENCV=1)
    target_include_directories(${TGT} PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(${TGT} PRIVATE ${OpenCV_LIBS})
  endif()
  if(ONNXRUNTIME_LIB)
    target_compile_definitions(${TGT} PRIVATE HAVE_ONNXRUNTIME=1)
    target_include_directories(${TGT} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(${TGT} PRIVATE ${ONNXRUNTIME_LIB})
  endif()
  if(SQLite3_FOUND)
    target_compile_definitions(${TGT} PRIVATE HAVE_SQLITE3=1)
    target_include_directories(${TGT} PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(${TGT} PRIVATE ${SQLite3_LIBRARIES})
  endif()
  if(CPPHTTPLIB_INCLUDE_DIR)
    target_compile_definitions(${TGT} PRIVATE HAVE_CPPHTTPLIB=1)
  endif()
endmacro()

apply_deps(edge_box)

if(NOT (OpenCV_FOUND AND ONNXRUNTIME_LIB))
  # 依赖不全时强制打开 mock 后端，保证流水线可运行（自测/CI）
  target_compile_definitions(edge_box PRIVATE EDGE_BOX_MOCK_VIDEO=1)
endif()

# ---------------- 自测 ----------------
enable_testing()
add_executable(edge_box_smoke tests/smoke_test.cpp ${EDGE_BOX_SHARED_SOURCES})
target_include_directories(edge_box_smoke PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_definitions(edge_box_smoke PRIVATE EDGE_BOX_MOCK_VIDEO=1)
apply_deps(edge_box_smoke)
add_test(NAME smoke COMMAND edge_box_smoke)

# Web 配置界面测试（真 HTTP，需 cpp-httplib）
if(CPPHTTPLIB_INCLUDE_DIR)
  add_executable(edge_box_web_test tests/web_test.cpp
    src/web/config_manager.cpp
    src/web/preview_store.cpp
    src/web/web_server.cpp
    src/common/image_codec.cpp
    src/config/config.cpp
    src/config/mini_json.cpp
    src/common/base64.cpp
    src/store/recognition_store.cpp
    src/backend/mock_backend.cpp
    src/face/face_align.cpp
    src/face/face_engine.cpp
  )
  target_include_directories(edge_box_web_test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  apply_deps(edge_box_web_test)
  add_test(NAME web_test COMMAND edge_box_web_test)
endif()