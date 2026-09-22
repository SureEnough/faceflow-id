# edge-box 边缘盒子人脸识别端（C++）

FaceFlow · 智脸客流人证系统 —— 边缘盒子端，负责多路 RTSP 视频实时人脸识别与客流统计。

## 功能

- 多路 RTSP/USB 摄像头接入（OpenCV VideoCapture，每路独立配置）
- 人脸检测/对齐/特征提取/活体（InsightFace SCRFD+ArcFace ONNX，推理后端可切换）
- 跨帧 IOU 跟踪（稳定 track_id，去重计次）
- 虚拟线客流判向（进/出/双向，每路独立口径），**内部人员（STAFF）排除在顾客客流外**
- 1:N 识别（区分顾客/内部人员，特征库由后台增量下发）
- 匿名轨迹入库（特征+抓拍+时间，**历史来访回查的数据基础**）
- 断网本地缓存 + 批量上报（幂等键 `track_id+camera_id+created_at`，含抓拍图快照）
- **多路线程池**（每路相机独立线程，文档 4.1.6；存储/上报线程安全）
- **设备注册与心跳**：启动自注册主设备+代注册摄像头，周期心跳（后台设备树在线实时）
- **后台配置下发**：轮询 `GET /devices/:id/config`，远程配置优先、自动热重载
- **Web 配置界面**（Vite+React+TS+Antd6）：运行状态监控、摄像头/阈值在线编辑、保存热重载、重启

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
│   ├── report/report_client.h/.cpp # 上报（cpp-httplib / 打印）
│   ├── report/features_sync.h/.cpp # 人员库增量同步
│   ├── report/device_ops.h/.cpp    # 设备注册/心跳/远程配置拉取
│   ├── web/                        # Web 配置服务（config_manager/web_server）
│   └── web/ (web/)                 # 前端工程（Vite+React+TS+Antd6，构建产物 web/dist）
└── tests/smoke_test.cpp            # 无依赖自测
```

## 构建

```bash
# Web 前端（可选，首次或前端代码变更后）
cd web && npm install && npm run build   # 产物 web/dist（由 C++ 服务托管）

# 依赖可选：OpenCV / ONNXRuntime / SQLite3 / cpp-httplib(third_party/httplib.h)
cmake -B build -DEDGE_BOX_WITH_OPENCV=ON -DEDGE_BOX_WITH_ONNX=ON
cmake --build build -j4

# 无任何第三方依赖时（mock 模式，用于自测/CI）：
cmake -B build -DEDGE_BOX_WITH_OPENCV=OFF -DEDGE_BOX_WITH_ONNX=OFF
cmake --build build -j4
ctest --test-dir build            # 自测（smoke + web_test）

# Web UI 回归（Playwright E2E，统一在仓库根 e2e/，详见 e2e/README.md）
cd e2e && npm install && npm run e2e:install && npm run e2e:edge
```

## 运行

```bash
cp config/edge_box.json.example edge_box.json   # 按门店改摄像头/虚拟线
./build/edge_box -c edge_box.json -backend mock
# 有模型时：
./build/edge_box -c edge_box.json -backend onnx
```

## Web 配置界面

配置开启（`web_enabled=true` 且设置 `web_password`）后，浏览器打开：

```
http://<边缘盒IP>:8180     # 默认端口 config.web_port
```

功能：
- **运行状态**：设备 ID / 后端 / 版本 / 运行时长 / 人员库版本 / 每路相机帧数与客流
- **全局参数**：阈值、上报端点、保留天数、活体/内部人员开关等，保存即原子写盘
- **摄像头**：增删改 URL、ID、角色、虚拟线；保存并应用会**热重载流水线**（不重启进程）
- **重启进程**：触发退出（生产由 systemd 自动拉起）

API（Basic Auth，`web_username`/`web_password`）：

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/status` | 运行状态 JSON |
| GET | `/api/config` | 当前配置（PSK/密码掩码） |
| PUT | `/api/config` | 更新配置（校验 + 原子写盘 + 置重载标记） |
| POST | `/api/reload` | 重新读取磁盘配置并热重载 |
| POST | `/api/restart` | 请求进程退出（守护进程拉起） |

> 安全提示：Web 服务监听 `0.0.0.0`，生产请通过防火墙仅放行门店内网，或置于 VPN/反代之后。

## 实现状态（增量）

- ✅ 多路线程池：每路相机独立线程 + 帧数/状态线程安全计数，多路不掉帧
- ✅ 抓拍图快照：新轨迹首帧编码对齐人脸图（OpenCV JPEG / 无依赖 BMP）随记录上报，后台对象存储转存
- ✅ 设备注册 + 心跳：`POST /devices/register` + `/devices/:id/heartbeat`（含子摄像头在线状态）
- ✅ 后台配置下发：边缘盒轮询应用远程配置并热重载（设备身份/Web 安全字段本地优先，防循环）
- ✅ Web 配置界面（C++ httplib Server + Vite/React/TS/Antd6 前端，Basic Auth）
- ✅ 配置热重载：摄像头增改立即生效（重建流水线），上报端点/PSK 变化自动重建客户端
- ✅ Web UI 浏览器验证（Playwright + Chromium 无头）：登录弹窗、状态页、摄像头页、参数页 13 项断言全过；本地打开 `http://<IP>:8180` 走查

## 关键设计说明

- **推理后端抽象**（`IInferenceBackend`）：业务代码不感知 ONNX/NCNN；`CreateBackend(kOnnx)` 缺库时 fallback Mock，保证流水线始终可跑。
- **匿名轨迹**：未命中人员库的脸同样入库（`customer_id=-1`），特征随记录上报后台 → 顾客录入后可按特征回查历史来访（核心理念见架构文档 7.2）。
- **内部人员排除**：1:N 命中 `person_type=1` 标记 STAFF，不入顾客客流；`count_flow=false` 的摄像头只识别不计数。
- **多路调度**：当前骨架为单线程轮询（`main.cpp`），生产按文档 4.1.6 拆为每路一线程 + 帧队列。

## 实现状态

- ✅ ONNXRuntime 后端核心：SCRFD 检测（多 stride anchor 解码、[C,N]/[N,k] 布局适配、IoU NMS、拼接单输出回退）、ArcFace 特征（BGR→RGB、(v-127.5)/128、L2 归一化）、RGB 活体（0~1 概率）
- ✅ 5 点相似变换对齐（`face_align.cpp`，ArcFace 标准 align，无 OpenCV 依赖）
- ✅ SQLiteStore：`recognition_logs` 表 + 唯一索引（track_id,camera_id,created_at）+ WAL，断网缓存/幂等去重
- ✅ cpp-httplib 上报：设备登录（`POST /auth/device/login`）、Bearer token、401 重登重试、RFC3339/幂等键与后台契约对齐
- ✅ 人员库增量同步：`features_sync.cpp` 轮询 `since_version`、base64 解码、失效移除、周期调度
- ⏳ 待真机标定：检测/识别阈值、模型输入尺寸（默认 640/112）、关键点启用
- ⏳ Web UI 可视化截图（浏览器打开 http://<IP>:8180 验证）

## 待完善（后续迭代）

- 抓拍图保存与上传（对象存储，见 `docs/deployment.md`）
- 多路线程池（文档 4.1.6，当前 mock 单线程轮询）