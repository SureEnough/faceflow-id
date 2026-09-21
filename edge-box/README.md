# edge-box 边缘盒子人脸识别端（C++）

智能人脸客流采集与身份证核验系统 —— 边缘盒子端，负责多路 RTSP 视频实时人脸识别与客流统计。

## 功能

- 多路 RTSP/USB 摄像头接入（OpenCV VideoCapture，每路独立配置）
- 人脸检测/对齐/特征提取/活体（InsightFace SCRFD+ArcFace ONNX，推理后端可切换）
- 跨帧 IOU 跟踪（稳定 track_id，去重计次）
- 虚拟线客流判向（进/出/双向，每路独立口径），**内部人员（STAFF）排除在顾客客流外**
- 1:N 识别（区分顾客/内部人员，特征库由后台增量下发）
- 匿名轨迹入库（特征+抓拍+时间，**历史来访回查的数据基础**）
- 断网本地缓存 + 批量上报（幂等键 `track_id+camera_id+created_at`）

## 目录结构

```
edge-box/
├── CMakeLists.txt
├── config/edge_box.json.example
├── src/
│   ├── main.cpp                    # 入口
│   ├── common/common.h             # 类型/日志
│   ├── config/                     # 配置（内置 MiniJson）
│   ├── backend/                    # 推理后端：inference_backend.h / mock_backend / onnx_backend
│   ├── face/face_engine.h/.cpp     # 检测→对齐→特征→活体组合
│   ├── video/video_source.h/.cpp   # 多路视频源（OpenCV / Mock 合成帧）
│   ├── pipeline/pipeline.h/.cpp    # 单路流水线
│   ├── tracker/iou_tracker.h/.cpp  # IOU 跟踪
│   ├── flow/flow_counter.h/.cpp    # 虚拟线判向
│   ├── recognizer/recognizer.h/.cpp# 1:N 余弦检索
│   ├── store/recognition_store.h/.cpp  # 记录存储（SQLite / 内存）
│   └── report/report_client.h/.cpp # 上报（cpp-httplib / 打印）
└── tests/smoke_test.cpp            # 无依赖自测
```

## 构建

```bash
# 依赖可选：OpenCV / ONNXRuntime / SQLite3 / cpp-httplib(third_party/httplib.h)
cmake -B build -DEDGE_BOX_WITH_OPENCV=ON -DEDGE_BOX_WITH_ONNX=ON
cmake --build build -j4

# 无任何第三方依赖时（mock 模式，用于自测/CI）：
cmake -B build -DEDGE_BOX_WITH_OPENCV=OFF -DEDGE_BOX_WITH_ONNX=OFF
cmake --build build -j4
ctest --test-dir build            # 自测
```

## 运行

```bash
cp config/edge_box.json.example edge_box.json   # 按门店改摄像头/虚拟线
./build/edge_box -c edge_box.json -backend mock
# 有模型时：
./build/edge_box -c edge_box.json -backend onnx
```

## 关键设计说明

- **推理后端抽象**（`IInferenceBackend`）：业务代码不感知 ONNX/NCNN；`CreateBackend(kOnnx)` 缺库时 fallback Mock，保证流水线始终可跑。
- **匿名轨迹**：未命中人员库的脸同样入库（`customer_id=-1`），特征随记录上报后台 → 顾客录入后可按特征回查历史来访（核心理念见架构文档 7.2）。
- **内部人员排除**：1:N 命中 `person_type=1` 标记 STAFF，不入顾客客流；`count_flow=false` 的摄像头只识别不计数。
- **多路调度**：当前骨架为单线程轮询（`main.cpp`），生产按文档 4.1.6 拆为每路一线程 + 帧队列。

## 待完善（骨架后续迭代）

- ONNXRuntime 后端完整实现（SCRFD decode/NMS、ArcFace 预处理、活体推理）
- 人员库增量同步（轮询 `GET /customers/features/sync`，内部人员仅收工号哈希）
- SQLite `recognition_store.cpp` 实现、断网续传补偿
- cpp-httplib 上报实现（组装 JSON、幂等去重、token 鉴权）
- 抓拍图保存与上传（对象存储）