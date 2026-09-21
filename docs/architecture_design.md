# 智能人脸客流采集与身份证核验系统

## 架构设计文档

| 项目 | 内容 |
|---|---|
| 文档版本 | V1.0 |
| 编写日期 | 2026-09-21 |
| 文档状态 | 初稿（评审稿） |
| 适用对象 | 研发、测试、运维、产品 |

---

## 1. 引言

### 1.1 项目背景

面向门店/场馆/园区等场景，需要一套 **C++ 实现的智能客流系统**：在顾客到访时自动完成人脸客流采集与统计，同时支持顾客以「身份证 + 人脸」方式完成身份录入与核验，并能够**回溯该顾客在录入之前的历史到访记录，统计累计来访次数**。

### 1.2 建设目标

- **客流采集**：边缘盒子对摄像头视频流进行实时人脸检测、抓拍、去重计数与进出方向判定。
- **人证核验**：录入端读取身份证信息（文字 + 证件照），配合现场人脸进行 1:1 比对与活体检测。
- **历史来访统计**：顾客完成录入后，回查历史识别记录，聚合统计该顾客的累计来访次数与来访天数。
- **集中管理**：管理后台统一维护人员库、设备、识别记录与统计报表。
- **全链路开源**：人脸算法采用开源方案，可离线部署、可控成本、保护数据隐私。

### 1.3 术语定义

| 术语 | 说明 |
|---|---|
| 边缘盒子 | 部署在门店本地的计算设备（如 RK3588 / Jetson / x86 工控机），承载视频流人脸识别与客流统计 |
| 录入端 | 门店电脑，配有身份证读卡器与 USB 摄像头，用于顾客身份录入与人证核验 |
| 管理后台 | 集中管理平台，提供人员库/设备/记录/报表管理能力 |
| 匿名轨迹 | 顾客未注册前，边缘盒子每次识别产生的一条匿名记录（含人脸特征、抓拍图、时间等），用于注册后的回溯匹配 |
| 特征向量 | 人脸识别模型输出的浮点向量（本项目为 512 维），用于 1:1/1:N 相似度计算 |
| 1:N 识别 | 将待识别特征与特征库比对，返回相似度最高且超过阈值的身份 |
| 1:1 核验 | 将现场人脸特征与证件照特征比对，判定是否同一人 |

---

## 2. 系统总体架构

### 2.1 逻辑架构

系统划分为 **三端**，逻辑上分为接入层、业务层、数据层：

```
┌───────────────────────────────────────────────────────────────────┐
│                           接入层（硬件/设备）                        │
│   摄像头(客流)   摄像头(录入)   身份证读卡器    边缘盒子(ARM/x86)      │
└───────────────────────────────┬───────────────────────────────────┘
                                 │
┌───────────────────────────────▼───────────────────────────────────┐
│                           业务层                                    │
│  ┌──────────────────┐   ┌──────────────────┐   ┌────────────────┐  │
│  │  边缘盒子识别端    │   │  顾客录入电脑端    │   │  管理后台        │  │
│  │  检测/跟踪/抓拍    │   │  身份证读取        │   │  人员库管理      │  │
│  │ 1:N 识别(顾客/员工)│   │  1:1 人证比对      │   │  设备管理        │  │
│  │  客流(排除员工)    │   │  活体检测          │   │  记录查询        │  │
│  │  匿名轨迹留存     │   │  历史来访回查      │   │  来访统计/报表   │  │
│  └──────────────────┘   └──────────────────┘   └────────────────┘  │
└───────────────────────────────┬───────────────────────────────────┘
                                 │
┌───────────────────────────────▼───────────────────────────────────┐
│                           数据层                                    │
│   边缘端 SQLite(本地缓存)   后台 MySQL/PostgreSQL   对象存储(抓拍图)   │
└───────────────────────────────────────────────────────────────────┘
```

### 2.2 物理部署架构

```
                   ┌────────────────────────────┐
                   │         管理后台            │
                   │  Web 前端 + REST API 服务    │
                   │  DB(MySQL/PostgreSQL)       │
                   │  对象存储(MinIO/S3)          │
                   └──────┬──────────▲──────────┘
                          │REST/MQTT │REST/MQTT
        ┌─────────────────┴──┐    ┌───┴─────────────────┐
        │  边缘盒子识别端(门店1)│    │  录入电脑端(门店1)    │
        │  ┌──────────────┐  │    │  ┌──────────────┐  │
        │  │ 客流处理服务  │◄─┼────┼──┤ 核验录入服务  │  │
        │  │ 摄像头×N     │  │    │  │ 身份证读卡器   │  │
        │  │ SQLite 缓存  │  │    │  │ USB 摄像头    │  │
        │  └──────────────┘  │    │  └──────────────┘  │
        └────────────────────┘    └────────────────────┘
```

> 说明：同一门店可部署 1 台边缘盒子 + 1 台录入电脑；边缘盒子与录入端可复用同一局域网；断网时各端本地运行，网络恢复后数据补偿上报。
> 设备层级：RTSP 摄像头注册为边缘盒子的子设备；USB 摄像头、身份证读卡器注册为录入电脑端的子设备；后台以树形展示在线状态。

### 2.3 技术选型总览

| 层次 | 选型 | 说明 |
|---|---|---|
| 人脸检测 | InsightFace SCRFD / RetinaFace（ONNX） | 检测率高、速度快，开源模型 |
| 人脸识别 | InsightFace ArcFace（ONNX，512 维） | 开源界 SOTA 基准 |
| 活体检测 | InsightFace 2.0 RGB Liveness（ONNX） | 防照片/视频/面具攻击 |
| 推理引擎 | ONNXRuntime C++（x86/Jetson）/ NCNN（RK 系列） | 通过推理后端抽象层切换 |
| 图像处理 | OpenCV 4.x | 采集、预处理、绘制 |
| 目标跟踪 | ByteTrack / 自研 IOU Tracker（C++） | 跨帧关联，支撑去重计数 |
| 特征检索 | 线性扫描（<10 万条）/ FAISS（大规模） | 历史来访回查 |
| 桌面 GUI | Qt 6（录入端） | 跨平台桌面程序 |
| 后台服务 | Go（Gin + GORM） | REST API、历史来访回查服务 |
| 后台前端 | Vite + React + TypeScript + Ant Design 6 | 管理界面 |
| 数据库 | 边缘端 SQLite；后台 MySQL 8 / PostgreSQL | 数据持久化 |
| 通信 | HTTP REST + WebSocket / MQTT(Mosquitto) | 数据上报与下发 |
| 构建 | CMake + vcpkg / Conan | 依赖管理 |---

## 3. 开源人脸算法选型

### 3.1 选型结论（基于 2026-09 现状核实）

| 方案 | 状态 | 结论 |
|---|---|---|
| **InsightFace**（deepinsight/insightface） | 29.8k stars，2026-09 持续更新；2.0 版新增 RGB 活体检测、InsightFace Server、INT8 量化 | ✅ **首选**，检测+识别+活体全家桶，开源界 SOTA |
| OpenCV Zoo（YuNet + SFace） | 模型迁移至 Hugging Face，跨 x86/ARM/RISC-V | 备选，适合轻量快速落地与低算力设备 |
| SeetaFace6 | 官方仓库 `seetafaceengine/SeetaFace6` 2021 年后停止更新，商用授权受限 | ❌ 不推荐用于新项目 |
| dlib / face_recognition | 模型较老（2017），活跃度低 | ❌ 不推荐用于新项目 |

### 3.2 采用的算法栈（InsightFace 系）

| 组件 | 模型 | 用途 |
|---|---|---|
| 人脸检测 | SCRFD（`scrfd_10g_bnkps` 或按算力选 2.5G/500M） | 检测人脸框 + 5 点关键点 |
| 人脸对齐 | 5 点关键点仿射变换（ArcFace 标准 align） | 归一化人脸区域 |
| 人脸识别 | ArcFace（`w600k_r50` 等，输出 512 维特征） | 特征提取、1:1/1:N |
| 活体检测 | InsightFace 2.0 RGB Liveness（`liveness` 模型） | 转录/核验端防伪 |

**模型部署矩阵**：

| 推理后端 | 适用平台 | 说明 |
|---|---|---|
| ONNXRuntime C++ | x86_64 工控机、NVIDIA Jetson | 开发调试首选，CPU/GPU 均支持 |
| NCNN | RK3588 / RK3399 / 树莓派等 ARM | 轻量、无依赖，RK NPU 可叠加加速 |
| TensorRT | 有 NVIDIA GPU 的服务器 | 高吞吐场景可选优化 |

> 工程上通过 `IInferenceBackend` 抽象接口切换后端，业务代码与推理引擎解耦（见 4.1.3 模块设计）。

### 3.3 相似度阈值与特征检索

- 特征维度：**512 维 float32**
- 相似度：**余弦相似度**
- 建议阈值（需按实际模型与场景标定）：
  - 1:1 人证比对：`similarity ≥ 0.50` 判定同一人（现场采集质量良好时）
  - 1:N 身份识别：`similarity ≥ 0.40` 候选入围，取最高者；0.38~0.40 区间为不确定带（可人工复核）
- 历史来访回查：采用与 1:N 相同的阈值策略，**允许一次录入回查出多条历史记录并聚合**（同一顾客多次来访的特征均应匹配成功）

### 3.4 许可证合规提示

| 内容 | License | 商用含义 |
|---|---|---|
| InsightFace 代码 | MIT | 可自由商用 |
| InsightFace 官方预训练模型 | 仅限非商业研究 | **商用需联系 insightface.ai 获得授权** |
| OpenCV / ONNXRuntime / NCNN / Qt | 各自开源协议（Apache-2.0 / MIT / LGPL 等） | 一般可商用，Qt 注意 LGPL 动态链接要求 |

> ⚠️ 若系统用于商业部署，请提前确认模型商用授权方案（可替换为自训练模型或购买商业授权）。

---

## 4. 各端详细设计

### 4.1 边缘盒子人脸识别端

#### 4.1.1 功能清单

| 功能 | 说明 |
|---|---|
| 视频接入 | 支持 **多路 RTSP**（RTSP/RTMP/USB）并发接入；每路独立采集/配置，掉线自动重连 |
| 人脸检测 | 逐帧检测人脸，输出框+关键点；按清晰度/角度/遮挡过滤低质量帧 |
| 人脸跟踪 | 跨帧 ID 关联，避免同一个人重复计次 |
| 质量评估 | 清晰度、亮度、人脸尺寸、俯仰角/偏转角阈值过滤 |
| 特征提取 | 对通过质量评估的人脸提取 512 维特征 |
| 1:N 识别 | 与本地人员库比对，命中则标记身份并区分 **顾客 / 内部人员（STAFF）** |
| 内部人员区分 | 内部人员命中后标记 STAFF，生成通行记录；**不计入顾客客流**，可按需单独统计 |
| 匿名轨迹 | 未命中身份的脸同样保存「匿名轨迹」（特征+抓拍+时间）——**历史来访回查的数据基础** |
| 客流统计 | 基于跟踪轨迹 + 虚拟线判向，统计**顾客**进/出/在店人数与时段客流；每路摄像头独立口径 |
| 数据上报 | 周期/即时将识别事件与客流数据上报后台；断网本地缓存、恢复后续传 |
| 本地配置 | 摄像头、阈值、上报地址等可配置 |

#### 4.1.2 处理流水线（帧级）

```
摄像头帧
   │
   ▼
[图像预处理] ──► [人脸检测 SCRFD] ──► 无人脸 → 丢弃/低频采样
   │
   ▼
[人脸跟踪 ByteTrack/IOU] —— 关联已有 track_id
   │
   ▼
[质量评估] —— 不合格 → 丢弃
   │
   ▼
[关键点对齐 → 裁剪归一化 ]
   │
   ├──────────────┬──────────────────────┐
   ▼              ▼                       ▼
[特征提取 ArcFace] [活体检测(可选)]     [抓拍图保存(JPEG)]
   │              │
   ▼              ▼
[1:N 人员库匹配]  [方向判定/虚拟线]         ┌─命中内部人员(STAFF)─► [STAFF 通行记录]
   │              │                              │        （不计顾客客流）
   ▼              ▼                              ▼
[识别结果+匿名轨迹] [顾客客流计数(排除STAFF)]  [人员类型标注]
   │              │
   └──────┬───────┘
          ▼
   [SQLite 本地写入] ──► [上报服务 → 管理后台]
```

#### 4.1.3 模块划分（C++ 目录结构）

```
edge-box/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                  # 入口：加载配置、启动各服务线程
│   ├── config/                   # 配置管理（json/yaml）
│   ├── video/                    # 摄像头/RTSP 接入（OpenCV VideoCapture）
│   ├── pipeline/                 # 帧流水线调度（多线程+队列）
│   ├── face/
│   │   ├── detector.h/.cpp       # 人脸检测封装（SCRFD）
│   │   ├── aligner.h/.cpp        # 关键点对齐
│   │   ├── extractor.h/.cpp      # 特征提取封装（ArcFace）
│   │   ├── liveness.h/.cpp       # 活体检测封装
│   │   └── quality.h/.cpp        # 质量评估
│   ├── backend/                  # 推理后端抽象
│   │   ├── inference_backend.h   # 抽象接口
│   │   ├── onnx_backend.cpp      # ONNXRuntime 实现
│   │   └── ncnn_backend.cpp      # NCNN 实现
│   ├── tracker/                  # 目标跟踪（ByteTrack/IOU）
│   ├── flow/                     # 客流统计（虚拟线、方向判定、去重）
│   ├── recognizer/               # 1:N 识别 + 特征库管理
│   ├── store/                    # SQLite 本地存储
│   ├── report/                   # 上报服务（REST/MQTT、断网续传）
│   └── common/                   # 日志、工具、编解码
└── models/                       # ONNX 模型文件（部署时放置）
```

#### 4.1.4 客流统计规则

- 在画面中设置一条**虚拟线**（可配置坐标）。
- 跟踪同一 `track_id`，当其中心点跨越虚拟线时：
  - 从上往下/从左往右 → `IN + 1`
  - 反向 → `OUT + 1`
- 同一 `track_id` 在镜头内存留期只计一次；离开视野后回收 ID。
- **内部人员排除**：1:N 命中 `person_type=1`（STAFF）的 track 标记为内部人员，不参与顾客客流计数；不命中的匿名 track 计入顾客客流（口径可配置）。
- **多路独立口径**：每路摄像头拥有独立虚拟线/方向/计数，`camera_id` 区分；同一顾客跨摄像头去重由后台按人聚合。
- 输出粒度：`日客流 / 时段客流 / 峰值 / 平均停留时长`；支持按 `person_type` 拆分。
- 单路 1080p@30fps 目标性能：检测+跟踪 ≥ 25 FPS（RK3588 NPU 或 x86 CPU 需合理选模型档位）。

#### 4.1.5 匿名轨迹留存策略

- 每次对**首次出现的 track_id**提取一次特征并保存：
  - 字段：`track_id, feature(512d blob), snapshot_path, camera_id, direction, person_type, created_at`
  - `person_type`：0 顾客（未识别或识别为顾客）/ 1 内部人员（STAFF）
- 留存策略（默认保留 **1 年**，可配置；隐私合规要求不可无限期留存）。
- 上报后台时同步特征，便于跨端回查；**历史来访回查仅统计顾客（person_type=0）**，内部人员不计。
- 多条摄像头轨迹合并：同一 `track_id` 仅保存质量最高一条；上报以 `track_id + camera_id + created_at` 唯一。

#### 4.1.6 多路 RTSP 摄像头接入设计

- **采集线程模型**：每路摄像头一个采集线程（OpenCV `VideoCapture` + RTSP），帧写入独立有界队列（默认容量 32，满则丢最旧帧，保证实时性）；推理线程池从队列取帧处理。
- **推理资源调度**：检测/提取共用推理后端实例与线程池（模型只加载一次）；按设备算力分配路数与模型档位：
  - RK3588（NPU）：建议 ≤ 4 路 1080p，SCRFD-2.5G 或量化模型；
  - x86 工控机（i5+）：建议 ≤ 8 路 1080p；
  - Jetson Orin：≤ 8 路，可开 TensorRT 加速。
- **每路独立配置**：`camera_id`、RTSP URL、虚拟线坐标、方向口径（进/出/双向）、是否计入客流、是否开启识别。
- **掉线自愈**：RTSP 断流指数退避重连（1s/2s/4s…/60s），恢复后自动续跑；长时间掉线上报告警。
- **时钟与对齐**：各路上报统一 UTC 时间戳；`camera_id` 全局唯一（`store_id + device_id + camera_no`）。

### 4.2 顾客录入电脑端

#### 4.2.1 功能清单

| 功能 | 说明 |
|---|---|
| 身份证读取 | 通过读卡器 SDK 读取姓名、性别、民族、出生日期、住址、身份证号、签发机关、有效期、**证件照** |
| 现场人脸采集 | USB 摄像头引导拍摄，自动检测、质量评估、抓拍最佳帧 |
| 1:1 人证比对 | 现场人脸特征 vs 证件照特征，显示相似度并判定 |
| 活体检测 | 现场 RGB 活体检测，杜绝照片/屏幕翻拍 |
| 历史来访回查 | **仅顾客**：录入成功后，用现场特征回查后台历史识别记录，聚合统计来访次数/天数/最近来访时间 |
| 人员入库/登记 | 新增/更新人员档案：顾客（姓名、身份证号、特征、照片）或**内部人员（工号、姓名、部门、特征、照片）** |
| 核验记录 | 每次核验生成记录（通过/不通过、相似度、时间、操作员） |
| 结果展示 | 实时界面：视频预览、读卡信息、比对分数、历史来访统计 |

#### 4.2.2 录入与核验流程（时序）

**通道 A：顾客人证核验 + 历史来访统计（默认）**

```
用户 ──► 读卡器SDK──► 身份证信息+证件照
用户 ──► USB摄像头 ──► 现场人脸检测/质量评估
                        │
                        ▼
                活体检测（RGB）
                        │ 通过
                        ▼
          [现场人脸特征] vs [证件照特征]  → 1:1 相似度
                        │
                   ┌────┴───────────┐
                 ≥ 阈值            < 阈值
                   │                 │
                   ▼                 ▼
        人证一致 ✔               人证不一致 ✘（可重试/人工复核）
                   │
                   ▼
        后台「历史来访回查服务」──► 顾客历史识别记录 1:N 检索聚合
                   │
                   ▼
        展示：该顾客历史来访 N 次 / M 天 / 最近来访时间
                   │
                   ▼
        更新人员库（person_type=0）──► 同步到各边缘盒子
```

**通道 B：内部人员录入（工号模式，跳过身份证）**

```
操作员/员工 ──► 输入工号，校验权限
员工 ──► USB摄像头 ──► 现场人脸检测/质量评估 → 活体检测
                        │ 通过
                        ▼
        后台检索：工号存在？
              ├─ 是 → 更新特征（或追加特征）
              └─ 否 → 新增内部人员档案（工号/姓名/部门，person_type=1）
                        │
                        ▼
        更新人员库并同步到各边缘盒子 → 后续该员工识别直接标记 STAFF
```

#### 4.2.3 模块划分（C++ / Qt）

```
enrollment-pc/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── ui/                       # Qt 界面（采集/录入/结果页）
│   ├── idcard/
│   │   ├── idcard_reader.h       # 读卡器抽象接口
│   │   ├── idcard_huawei.cpp     # 华视 CVR-100U 等实现（厂商 SDK）
│   │   └── idcard_jinglun.cpp    # 精伦 IDR210 等实现（厂商 SDK）
│   │   └── idcard_simulator.cpp  # 模拟器（供开发/演示）
│   ├── capture/                  # 摄像头采集、引导拍摄
│   ├── face/                     # 与边缘端共用的人脸算法封装（复用餐户）
│   ├── verify/                   # 1:1 比对 + 活体 + 阈值判定
│   ├── history/                  # 历史来访回查客户端
│   ├── api/                      # 后台 REST 客户端
│   └── common/
```

> 读卡器采用**硬件抽象层**：不同厂商 SDK 实现同一接口（读取、复位、错误码），业务层不感知具体设备；开发期可用模拟器联调。
> 内部人员与顾客共用 `api/customers`（以 `person_type` 区分）与同一特征库，录入端无需单独维护内部人员本地库。

### 4.3 管理后台

#### 4.3.1 功能清单

| 功能 | 说明 |
|---|---|
| 人员库管理 | **人员档案**（顾客 `person_type=0` / 内部人员 `person_type=1`）增删改查、照片/特征管理、黑名单 |
| 内部人员管理 | 工号、姓名、部门、岗位、在职状态；批量导入（Excel）|
| 设备管理 | **设备树**（门店→主设备→子设备）查看 5 类设备（边缘盒子/RTSP摄像头/录入电脑端/USB摄像头/身份证读卡器）在线状态、注册与配置下发 |
| 识别记录查询 | 按时间/门店/设备/身份筛选识别事件，查看抓拍图 |
| 客流统计报表 | 日/周/月**顾客**客流、进出趋势、时段分布、峰值；按门店对比；支持按摄像头拆分 |
| 员工通行记录 | 内部人员进出记录查询（门店/人员/时段） |
| 核验记录 | 人证核验通过/失败记录、相似度分布 |
| 历史来访回查服务 | 接收录入端请求，对历史识别记录做 1:N 特征检索并聚合统计 |
| 人员库下发 | 将顾客特征/黑名单增量同步至各边缘盒子 |
| 系统管理 | 账号、角色、权限、操作日志 |

#### 4.3.2 模块划分（建议）

```
admin-backend/                # Go 后端
├── cmd/
│   └── server/main.go        # 启动入口
├── internal/
│   ├── api/                  # REST 路由（Gin）
│   │   ├── customers.go      # 人员库
│   │   ├── devices.go        # 设备
│   │   ├── records.go        # 识别/核验记录
│   │   ├── stats.go          # 客流统计
│   │   └── history_search.go # 历史来访回查（1:N 检索聚合）
│   ├── service/              # 业务逻辑
│   ├── storage/              # GORM、特征向量读写
│   ├── search/               # 特征检索（线性扫描 / FAISS via cgo）
│   └── sync/                 # 设备端数据下发（增量）
├── pkg/                      # 通用包（编解码、日志、鉴权）
├── web/                      # 前端工程（Vite 构建产物）
│   ├── src/
│   │   ├── pages/            # 页面（人员库/设备/记录/报表）
│   │   ├── components/       # 通用组件
│   │   ├── api/              # axios 接口封装
│   │   ├── stores/           # 状态管理（zustand）
│   │   └── types/            # TS 类型定义
│   └── vite.config.ts
└── go.mod
```

> 管理后台：**Go（Gin + GORM + SQLite/MySQL）** 提供 REST API，历史来访回查在 Go 内实现 1:N 特征检索（<10 万条纯 Go 线性扫描；大数据量可经 cgo 集成 FAISS）。前端 **Vite + React + TypeScript + Ant Design 6**，`web/` 构建产物由 Go 服务静态托管或独立 Nginx 部署，接口契约不变。---

## 5. 数据设计

### 5.1 ER 关系

```
devices ──1:N──► recognition_logs（识别记录，含匿名轨迹；设备含多路 camera_id）
customers ──1:N──► recognition_logs（命中人员库后回填 customer_id；person_type 区分顾客/内部人员）
devices ──1:N──► verify_records（核验记录，仅顾客）
customers ──1:N──► verify_records
customers ──1:N──► visit_stats（顾客历史来访聚合结果，可选物化）
```

### 5.2 核心表结构

#### 5.2.1 customers（人员档案，后台；含顾客与内部人员）

| 字段 | 类型 | 说明 |
|---|---|---|
| id | BIGINT PK | 自增 |
| person_type | TINYINT | **0 顾客 / 1 内部人员（STAFF）** |
| name | VARCHAR(64) | 姓名 |
| id_card_no | CHAR(18) NULL | 身份证号（仅顾客；唯一索引；加密存储） |
| staff_no | VARCHAR(32) NULL | 工号（仅内部人员；唯一索引） |
| department | VARCHAR(64) NULL | 部门/岗位（仅内部人员） |
| gender | TINYINT | 性别 |
| birth_date | BIGINT | 出生日期（Unix 秒，当日 00:00Z） |
| address | VARCHAR(255) | 住址（加密存储，仅顾客） |
| id_photo_path | VARCHAR(255) | 证件照路径 |
| live_photo_path | VARCHAR(255) | 现场照路径 |
| face_feature | BLOB | 512 维特征（float32） |
| status | TINYINT | 0 正常 / 1 黑名单 / 2 注销 / 3 离职 |
| created_at / updated_at | BIGINT | 创建/更新时间（Unix 秒） |
| created_by | BIGINT | 录入操作员 |

#### 5.2.2 recognition_logs（识别记录 / 匿名轨迹）

| 字段 | 类型 | 说明 |
|---|---|---|
| id | BIGINT PK | |
| device_id | BIGINT | 边缘盒子 ID（FK→devices） |
| track_id | VARCHAR(64) | 边缘盒子本次出现的跟踪 ID |
| customer_id | BIGINT NULL | 命中人员库后回填；未命中为 NULL（匿名） |
| person_type | TINYINT | **0 顾客/匿名顾客 / 1 内部人员**；识别时回填 |
| face_feature | BLOB | 512 维特征 —— **回查的关键字段** |
| snapshot_path | VARCHAR(255) | 抓拍图路径 |
| similarity | FLOAT | 命中时的相似度（匿名时为 0） |
| direction | TINYINT | 0 进 / 1 出 |
| camera_id | VARCHAR(32) | 摄像头标识 |
| created_at | BIGINT | 识别时间（Unix 秒，索引） |
| sync_status | TINYINT | 0 待上报 / 1 已上报 |

> 索引：(device_id, created_at)、created_at、customer_id；特征检索单独走向量检索。

#### 5.2.3 verify_records（人证核验记录）

| 字段 | 类型 | 说明 |
|---|---|---|
| id | BIGINT PK | |
| customer_id | BIGINT | |
| id_card_no | CHAR(18) | 本次读取的证号（快照） |
| verify_result | TINYINT | 0 待定 / 1 通过 / 2 不通过 |
| similarity | FLOAT | 1:1 相似度 |
| liveness_score | FLOAT | 活体得分 |
| live_photo_path | VARCHAR(255) | 本次现场照 |
| device_id | BIGINT | 录入端设备 |
| operator | VARCHAR(64) | 操作员 |
| created_at | BIGINT | 创建时间（Unix 秒） |

#### 5.2.4 devices（设备，含层级关系）

| 字段 | 类型 | 说明 |
|---|---|---|
| id | BIGINT PK | |
| device_type | TINYINT | **1 边缘盒子 / 2 录入电脑端 / 3 RTSP 摄像头 / 4 USB 摄像头 / 5 身份证读卡器** |
| parent_id | BIGINT NULL | 父级设备（自引用）：RTSP 摄像头→边缘盒子；USB 摄像头/身份证读卡器→录入电脑端；主设备为 NULL |
| name | VARCHAR(64) | 设备名 |
| device_key | VARCHAR(64) | 设备标识：边缘盒/录入端=设备序列号；RTSP 摄像头=camera_id；USB 摄像头=usb 通道；读卡器=读卡器编号 |
| store_id | BIGINT | 门店 |
| status | TINYINT | 0 离线 / 1 在线 |
| last_heartbeat | BIGINT | 最后心跳（Unix 秒，子设备为父设备心跳携带时间） |
| config_json | JSON | 主设备配置（摄像头数组每路独立：URL/虚拟线/口径）；子设备可空 |

> **在线状态机制**：
> - 主设备（边缘盒子、录入电脑端）：自身心跳，超时（默认 5 分钟无心跳）判离线；
> - 子设备（RTSP 摄像头、USB 摄像头、身份证读卡器）：**由父设备心跳托管上报**在线状态（见 13.1 心跳报文）；父设备离线时子设备标记为「链路离线」。
> - 后台「设备管理」以树形展示：门店 → 主设备 → 子设备。

#### 5.2.5 visit_stats（来访统计，可选物化）

| 字段 | 类型 | 说明 |
|---|---|---|
| customer_id | BIGINT PK | |
| total_visits | INT | 累计来访次数（按识别记录条数，同一天多次计多次或按需去重） |
| visit_days | INT | 来访天数（按天去重） |
| first_visit_at | BIGINT | 首次到访（Unix 秒） |
| last_visit_at | BIGINT | 最近到访（Unix 秒） |
| updated_at | BIGINT | 更新时间（Unix 秒） |

> 说明：回查统计口径可配置——「按次」还是「按天」。默认展示两者：累计 N 次、覆盖 M 天。

### 5.3 特征向量存储与检索

- **存储**：BLOB 存 512×float32（2 KB/条）；可选 base64 JSON，兼顾跨端解析。
- **检索**：
  - 数据量 < 10 万条：SQL 读出特征 + 内存余弦计算（单线程约 <100 ms，可接受）。
  - 数据量 ≥ 10 万条：引入 **FAISS**（IndexFlatIP 内积即余弦，需先归一化），毫秒级返回 Top-K。
- **回查聚合**：1:N 检索 Top-K（K=50）→ 相似度≥阈值 → 过滤重复人员（特征聚类）→ 按 `customer_id` 聚合统计次数/天数（首次为匿名轨迹时按特征相似度归并为同一人）。

---

## 6. 接口设计

### 6.1 REST API 总览（前缀 `/api/v1`）

| 方法 | 路径 | 说明 | 调用方 |
|---|---|---|---|
| POST | /devices/register | 设备注册（5 类；子设备由父设备代为注册） | 边缘盒/录入端 |
| POST | /devices/{id}/heartbeat | 设备心跳；携带子设备在线状态 | 边缘盒/录入端 |
| GET | /devices | 设备树查询（含父子层级与在线状态） | 后台 |
| GET | /devices/{id}/config | 拉取设备配置 | 边缘盒/录入端 |
| POST | /records/recognition/batch | 批量上报识别记录（含匿名轨迹特征） | 边缘盒 |
| POST | /records/verify | 上报核验记录 | 录入端 |
| GET | /customers | 查询人员库 | 后台/录入端 |
| POST | /customers | 新增/更新顾客（录入接口） | 录入端 |
| POST | /customers/{id}/features/sync | 人员库增量拉取（特征） | 边缘盒 |
| POST | /history/search | **历史来访回查**：上传特征，返回聚合统计 | 录入端 |
| GET | /stats/flow | 客流统计（日/周/月/时段） | 后台 |
| GET | /stats/visits/{customer_id} | 单顾客来访统计 | 后台/录入端 |

### 6.2 关键接口报文示例

**历史来访回查（POST /history/search）**

```json
// 请求
{
  "customer_id": 1024,
  "id_card_no": "110101199001011234",
  "face_feature": "<base64, 512 float32>",
  "similarity_threshold": 0.40,
  "scope": { "store_ids": [1, 2], "start_at": "2025-09-21", "end_at": "2026-09-21" }
}

// 响应
{
  "code": 0,
  "data": {
    "total_visits": 23,          // 累计来访次数
    "visit_days": 9,             // 来访天数
    "first_visit_at": "2025-11-02 10:12:05",
    "last_visit_at": "2026-09-20 18:30:11",
    "matched_records": [
      { "log_id": 88123, "device_id": 1, "store_id": 1,
        "snapshot": "/snap/88123.jpg", "created_at": "2026-09-20 18:30:11", "similarity": 0.83 }
    ]
  }
}
```

**识别记录批量上报（POST /records/recognition/batch）**

```json
{
  "device_id": 3,
  "records": [
    {
      "track_id": "T-88-102",
      "customer_id": null,
      "person_type": 0,
      "face_feature": "<base64 512 float32>",
      "snapshot_path": "3/2026/09/20/88123.jpg",
      "direction": 0,
      "camera_id": "cam-01",
      "created_at": "2026-09-20T18:30:11Z"
    }
  ]
}
```

### 6.3 人员库同步与断网续传

- **人员库下发**：边缘盒定期轮询 `GET /customers/{id}/features/sync`（增量版本号），或后台通过 WebSocket/MQTT 即时推送；下发内容包含 `customer_id + 特征`，不包含明文身份证信息。
- **断网续传**：边缘盒/录入端本地 SQLite 缓存待上报数据（`sync_status=0`），网络恢复后按时间分批补偿上报，后台以 `track_id + device_id + created_at` 做幂等去重。
- **时钟同步**：设备端上报统一使用 UTC 时间戳，后台展示本地化时间，避免跨时区/时钟漂移导致统计错乱。

---

## 7. 核心业务流程

### 7.1 客流识别上报（常态）

```
各路摄像头帧（独立采集线程）→ 检测 → 跟踪(新 track_id) → 质量评估 → 特征提取
   → 1:N 匹配（命中顾客 → 标注 customer_id, person_type=0；命中内部人员 → 标注 STAFF, person_type=1；未命中 → 匿名 person_type=0）
   → 方向判定 → 顾客客流计数（STAFF 排除；count_flow=false 路只识别不计数）
   → 写本地 recognition_logs（含特征+抓拍+person_type）
   → 批量上报后台（幂等键含 camera_id）
```

### 7.2 顾客录入 + 历史来访统计（核心新需求）

```
身份证读取 + 现场人脸采集
   → 活体检测
   → 1:1 人证比对（现场 vs 证件照）
        → 不通过：拒绝并记录 verify_record
        → 通过：
            ├─ 新增/更新 customers（特征、照片）
            ├─ 调用 /history/search 历史回查
            │     → 后台在历史 recognition_logs 中做 1:N 特征检索
            │     → 聚合：总次数/天数/首次/最近来访
            └─ 展示历史来访结果 + 写 verify_record
   → 触发人员库增量同步到各边缘盒子（后续该顾客识别时直接带名）
```

### 7.3 一致性说明

- 顾客录入**前**的历史记录是匿名轨迹（`customer_id = NULL`），通过特征相似度归并统计；
- 顾客录入**后**的新识别记录直接回填 `customer_id`，统计不断累积；
- 同一顾客多次录入（特征可能因年龄/妆容变化）时，建议保留最新特征并允许「特征列表」多特征匹配，提升回查召回率。
- 内部人员（person_type=1）**不参与**历史来访回查与顾客客流统计；其通行记录独立留存，可在后台按工号/部门查询。---

## 8. 非功能设计

### 8.1 性能指标（目标值）

| 指标 | 目标 | 备注 |
|---|---|---|
| 边缘盒单路检测帧率 | ≥ 25 FPS（1080p） | RK3588 NPU / x86 CPU 按模型档位调优 |
| 识别时延（检测→特征→比对） | ≤ 200 ms/次 | 不含网络传输 |
| 人证 1:1 比对时延 | ≤ 500 ms | 本地推理 |
| 历史回查（<10 万条记录） | ≤ 1 s | 线性扫描 + 内存计算 |
| 历史回查（≥10 万条，FAISS） | ≤ 100 ms | 归一化 + IndexFlatIP |
| 上报失败补偿 | 断网 24 h 内不丢数据 | SQLite 本地缓存 |
| 后台报表查询 | 常规查询 ≤ 3 s | 关键字段建索引、统计表物化 |

### 8.2 安全设计

- **数据传输**：端到后台采用 HTTPS；不上报时本地驻留在可信内网。
- **隐私数据加密**：身份证号、姓名、住址等敏感字段在数据库中加密（AES-256-GCM），特征向量不视为明文身份信息但仍受访问控制保护。
- **访问控制**：管理后台 RBAC（管理员/操作员/只读）；设备注册使用预共享密钥（PSK）注册换取 token。
- **审计日志**：所有人员档案新增/修改/删除、核验操作记录操作日志。
- **最小化原则**：边缘盒只获取「特征 + 抓拍」，不获取身份证明文；录入端负责敏感数据读取。

### 8.3 可靠性

- 边缘盒本地 SQLite 长期运行需定期 `VACUUM` 与存档清理（按留存策略滚动删除旧记录并备份）。
- 后台数据库定时备份；抓拍/证件照进入对象存储并设置生命周期规则。
- 设备异常（摄像头掉线、推理进程崩溃）自动重启并上报告警；边缘盒看门狗自愈。

### 8.4 可扩展性

- 门店横向扩展：新边缘盒注册即可接入，无需改后台代码。
- 特征检索横向扩展：数据量大时可平滑迁移到 FAISS / Milvus（开源向量数据库）。
- 算法可替换：通过 `IInferenceBackend` 与模型配置化，可替换检测/识别模型（如换档位模型平衡精度与算力）。
- 客流规则可配置：虚拟线坐标、时间段、统计口径均支持后台下发配置。

---

## 9. 部署与运维

### 9.1 部署拓扑

| 组件 | 建议配置 |
|---|---|
| 边缘盒子 | RK3588（8G+）/ Jetson Orin Nano / x86 工控机（i5+）；Ubuntu 22.04/CentOS；摄像头 RTSP |
| 录入电脑 | Windows 10/11 或 Ubuntu；USB 摄像头；身份证读卡器（华视/精伦等）；Qt 6 运行时 |
| 后台服务器 | 2C4G 起步，随门店数量扩展；Nginx 反代 + 应用服务 + MySQL + 对象存储 |
| 网络 | 门店与企业内网/VPN；断网可离线运行 |

### 9.2 关键配置项

- 边缘盒：`camera_urls`、`det_thresh`、`recog_thresh`、`virtual_line`、`report_endpoint`、`retention_days`。
- 录入端：`idcard_reader_type`、`liveness_enabled`、`verify_threshold`、`server_endpoint`。
- 后台：`db`、`object_storage`、`sync_interval`、`retention_days`、`faiss_enabled`。

### 9.3 运维要点

- 设备心跳监控 + 告警（离线、识别中断、磁盘空间不足）。
- 日志集中收集（结构化日志，按天滚动）。
- 模型与配置版本管理，支持灰度下发。

---

## 10. 合规与风险

| 风险/事项 | 说明 | 应对 |
|---|---|---|
| 人脸模型商用授权 | InsightFace 官方预训练模型仅限非商业研究 | 商用前获得授权；或替换为自有/授权训练模型 |
| 个人信息保护法（PIPL） | 采集人脸、身份证信息属于敏感个人信息 | 告知同意、最小必要、加密存储、留存期限、提供删除渠道 |
| 身份证读卡器合规 | 读取二代证需公安部认证的读卡器及厂商 SDK | 读卡器硬件抽象层 + 厂商授权对接，不自行实现读卡协议 |
| 误识别风险 | 1:N 阈值过低将导致误匹配 | 阈值标定、人证核验人工复核兜底、误报日志分析 |
| 活体绕过 | 照片/屏幕/面具攻击 | 默认开启 RGB 活体，必要时叠加动作指令活体 |
| 特征库规模 | 长期运行特征库膨胀 | 留存策略 + 定期清理 + FAISS 扩容 |

---

## 11. 里程碑规划（建议）

| 阶段 | 内容 | 产出 |
|---|---|---|
| M1 核心算法验证 | 模型下载/转 ONNX、C++ 推理 Demo（检测/特征/1:1） | 算法可行性报告 |
| M2 边缘盒客流 | 视频接入、检测跟踪、客流统计、匿名轨迹入库上报 | 可演示的客流盒子 |
| M3 录入端 | 读卡器抽象、1:1 核验、活体、Qt 界面 | 可演示的录入端 |
| M4 后台与回查 | 人员库、设备管理、历史回查服务（1:N 聚合）、报表 | 三端闭环 |
| M5 联调与试点 | 端到端联调、阈值标定、试点门店部署 | 上线验收 |

---

## 12. 待确认事项（影响开发）

| # | 事项 | 影响 |
|---|---|---|
| 1 | 边缘盒子硬件平台（RK3588 / Jetson / x86） | 推理后端选型（NCNN / ONNXRuntime / TensorRT） |
| 2 | 身份证读卡器型号（华视/精伦/其他） | 读卡器 SDK 适配 |
| 3 | 历史记录留存时长（默认 1 年） | 特征库规模与检索方案 |
| 4 | ~~管理后台技术栈~~ 已确认：Go + Vite + React + TypeScript + Ant Design 6 | 后端与前端工程采用该栈 |
| 5 | 门店数量与单店摄像头路数 | 部署规格与性能指标 |

---

## 13. 详细接口定义（OpenAPI 风格）

> 本节按端给出完整接口契约；后端（Go + Gin）可据此直接生成 OpenAPI 3.0 规范与前端 TS 类型（openapi-generator / swag）。

### 13.0 通用约定

- 统一前缀：`/api/v1`
- 统一响应体：

```json
{ "code": 0, "message": "ok", "data": { } }
```

| code | 含义 |
|---|---|
| 0 | 成功 |
| 40001 | 参数错误 |
| 40100 | 未认证 / token 失效 |
| 40300 | 无权限 |
| 40400 | 资源不存在 |
| 42900 | 频率超限 |
| 50000 | 服务内部错误 |

- 鉴权：设备与录入端通过 `Authorization: Bearer <token>`；token 由 PSK 注册换取。
- 时间字段：**数据库统一存储 BIGINT Unix 秒（int64）**，适配 SQLite/MySQL/PostgreSQL，避免 DATETIME 方言差异；API 传输层统一 ISO8601（UTC），如 `2026-09-20T10:30:11Z`，由服务端在读写边界转换。
- 字段命名：snake_case；特征向量 `face_feature` 为 base64（512×float32 = 2048 字节 → base64 2728 字符）。

### 13.1 设备接入

#### POST /devices/register（设备注册）

请求：

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| device_type | int | 是 | **1 边缘盒子 / 2 录入电脑端 / 3 RTSP 摄像头 / 4 USB 摄像头 / 5 身份证读卡器** |
| parent_id | int | 否 | 父级设备 ID：RTSP 摄像头→边缘盒子；USB 摄像头/读卡器→录入电脑端；主设备不填 |
| device_key | string | 否 | 标识：camera_id / usb 通道 / 读卡器编号（子设备必填） |
| name | string | 是 | 设备名 |
| store_id | int | 是 | 所属门店 |
| psk | string | 是 | 预共享密钥（后台预分配） |

> 注册主体：边缘盒子/录入电脑端启动时自注册；**RTSP 摄像头、USB 摄像头、身份证读卡器由父设备在启动/配置变更时批量代为注册**（带 `parent_id`），子设备无独立 psk。

响应 data：

```json
{ "device_id": 3, "token": "eyJhbGciOi...", "expires_at": "2026-10-20T10:30:11Z" }
```

#### POST /devices/{id}/heartbeat（心跳/状态上报）

请求（主设备心跳，携带子设备在线状态）：

```json
{
  "status": 1,
  "cpu": 42.5, "mem": 61.2, "disk": 35.0, "fps": 27.0,
  "sub_devices": [
    { "device_key": "cam-01", "type": 3, "online": true,  "bitrate_kbps": 4200, "fps": 25 },
    { "device_key": "cam-02", "type": 3, "online": false }
  ]
}
```

**录入电脑端心跳扩展**：

```json
{
  "status": 1, "cpu": 18.0, "mem": 45.0, "disk": 20.0,
  "sub_devices": [
    { "device_key": "usb0",   "type": 4, "online": true  },
    { "device_key": "reader-01", "type": 5, "online": true }
  ]
}
```

> 后台根据 `sub_devices` 更新子设备 `status`；主设备心跳超时（5 分钟）则将整棵子树标记离线/链路未知。

#### GET /devices（设备树查询）

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| store_id | int | 否 | 门店 |
| device_type | int | 否 | 按类型过滤 |
| online | int | 否 | 0 离线 / 1 在线 |

响应 data（含父子层级，前端组装树）：

```json
{
  "items": [
    { "id": 3, "device_type": 1, "name": "边缘盒子-01", "parent_id": null, "store_id": 1, "status": 1,
      "children": [
        { "id": 8, "device_type": 3, "name": "RTSP摄像头-入口", "parent_id": 3, "store_id": 1, "status": 1, "device_key": "cam-01" }
      ] },
    { "id": 5, "device_type": 2, "name": "录入电脑-01", "parent_id": null, "store_id": 1, "status": 1,
      "children": [
        { "id": 9, "device_type": 4, "name": "USB摄像头", "parent_id": 5, "store_id": 1, "status": 1, "device_key": "usb0" },
        { "id": 10, "device_type": 5, "name": "身份证读卡器", "parent_id": 5, "store_id": 1, "status": 1, "device_key": "reader-01" }
      ] }
  ]
}
```

#### GET /devices/{id}/config（拉取配置）

响应 data：

```json
{
  "cameras": [
    {
      "camera_id": "cam-01",
      "url": "rtsp://192.168.1.20/stream1",
      "role": "entrance",
      "count_flow": true,
      "virtual_line": { "x1": 200, "y1": 540, "x2": 1080, "y2": 540 },
      "direction": "both"
    },
    {
      "camera_id": "cam-02",
      "url": "rtsp://192.168.1.21/stream1",
      "role": "counter",
      "count_flow": false,
      "virtual_line": null
    }
  ],
  "det_thresh": 0.5,
  "recog_thresh": 0.4,
  "verify_thresh": 0.5,
  "liveness_enabled": true,
  "staff_enabled": true,
  "retention_days": 365,
  "report_interval_s": 60
}
```
> 说明：`staff_enabled=true` 时边缘盒启用内部人员特征库做 STAFF 识别并排除出顾客客流；`count_flow=false` 的摄像头只识别不计数（如柜台/收银视角）。

### 13.2 识别与核验记录上报

#### POST /records/recognition/batch（边缘盒批量上报）

见 6.2 报文；补充分页/幂等要求：`max 200 条/批`，后台以 `device_id + track_id + camera_id + created_at` 唯一去重；报文每条含 `person_type`。

#### POST /records/verify（录入端核验记录）

请求：

```json
{
  "device_id": 5,
  "customer_id": 1024,
  "id_card_no": "110101199001011234",
  "verify_result": 1,
  "similarity": 0.87,
  "liveness_score": 0.98,
  "live_photo": "<base64 jpg>",
  "operator": "admin"
}
```

### 13.3 人员库

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | /customers | 分页查询人员库；参数：page,page_size,person_type,name,id_card_no,staff_no,status |
| POST | /customers | 新增人员档案（顾客或内部人员，`person_type` 区分；录入端调用） |
| PUT | /customers/{id} | 更新档案 |
| DELETE | /customers/{id} | 删除（软删 status=2/3） |
| POST | /customers/{id}/features | 追加特征（多特征支持） |
| GET | /customers/features/sync | 增量拉取（边缘盒）；参数：since_version |

POST /customers 请求示例（顾客通道 A）：

```json
{
  "person_type": 0,
  "name": "张三",
  "id_card_no": "110101199001011234",
  "gender": 1,
  "birth_date": "1990-01-01",
  "address": "北京市朝阳区...",
  "id_photo": "<base64 jpg>",
  "live_photo": "<base64 jpg>",
  "face_feature": "<base64 512 float32>"
}
```

POST /customers 请求示例（内部人员通道 B）：

```json
{
  "person_type": 1,
  "name": "李四",
  "staff_no": "E1024",
  "department": "运营部",
  "live_photo": "<base64 jpg>",
  "face_feature": "<base64 512 float32>"
}
```

响应 data：

```json
{ "customer_id": 1024, "version": 17, "history": {
    "total_visits": 23, "visit_days": 9,
    "first_visit_at": "2025-11-02T02:12:05Z",
    "last_visit_at": "2026-09-20T10:30:11Z" } }
```

> 说明：POST /customers 即「录入接口」，后台在写入档案后**同步触发历史回查**并把聚合结果一并返回，录入端无需再单独调 /history/search。

### 13.4 历史来访回查

#### POST /history/search

见 6.2 报文；补充：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| face_feature | string(base64) | 是 | 现场人脸特征（或证件照特征） |
| customer_id | int | 否 | 已注册则直接按特征+ID 双路检索 |
| similarity_threshold | float | 否 | 默认 0.40 |
| scope.store_ids | int[] | 否 | 门店范围，空=全部 |
| scope.start_at / end_at | string | 否 | 时间范围 |
| top_k | int | 否 | 默认 50 |
| scope.person_type | int | 否 | 固定按 0（仅顾客），内部人员不参与 |

响应 data（聚合结果）见 6.2；`matched_records` 按时间倒序，最多返回 `top_k` 条。

### 13.5 统计报表

#### GET /stats/flow（顾客客流统计）

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| store_ids | int[] | 否 | 门店 |
| device_ids | int[] | 否 | 设备 |
| camera_ids | string[] | 否 | 摄像头 |
| person_type | int | 否 | 默认 0（顾客）；1 返回内部人员口径 |
| granularity | string | 是 | day / week / month / hour |
| start_at / end_at | string | 是 | 时间范围 |

响应 data：

```json
{
  "items": [
    { "bucket": "2026-09-20", "in": 1203, "out": 1180, "peak": 156, "peak_hour": "19:00" }
  ],
  "total": { "in": 1203, "out": 1180, "unique": 986 }
}
```

> 客流统计口径默认**仅顾客**（person_type=0，含匿名顾客）；内部人员由 `/stats/staff` 单独统计。

#### GET /stats/staff（内部人员通行记录/统计）

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| staff_no | string | 否 | 工号过滤 |
| department | string | 否 | 部门过滤 |
| start_at / end_at | string | 是 | 时间范围 |

响应 data：

```json
{
  "items": [
    { "customer_id": 9001, "staff_no": "E1024", "department": "运营部",
      "in": 2, "out": 2, "last_in": "2026-09-20T01:02:03Z" }
  ]
}
```

#### GET /stats/visits/{customer_id}（单顾客来访统计）

响应 data（同 6.2 的 data 结构）。

### 13.6 人员库同步下发

- 边缘盒轮询：`GET /customers/features/sync?since_version=<v>`，响应增量：

```json
{
  "base_version": 17,
  "new_version": 20,
  "items": [
    { "customer_id": 1024, "person_type": 0, "version": 17, "face_feature": "<base64>", "status": 0 },
    { "customer_id": 9001, "person_type": 1, "staff_no_hash": "<sha256>", "version": 18, "face_feature": "<base64>", "status": 0 }
  ]
}
```
> 内部人员同步到边缘盒时仅下发 `staff_no_hash`（工号哈希）与特征，不下发明文工号/姓名，降低边缘端敏感数据暴露面。

- 后台 WebSocket `/ws/sync`：有变更时主动推送 `{ "type": "customer_update", "version": 20 }`，边缘盒收到后轮询增量接口拉取。---

## 14. 数据库 DDL

### 14.1 管理后台（MySQL 8.x）

```sql
-- 门店
CREATE TABLE stores (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  name          VARCHAR(64)  NOT NULL,
  address       VARCHAR(255) DEFAULT '',
  status        TINYINT      NOT NULL DEFAULT 1 COMMENT '1 营业 / 0 停用',
  created_at    BIGINT       NOT NULL,                -- Unix 秒（应用层写入）
  updated_at    BIGINT       NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 设备（5 类，支持父子层级：RTSP摄像头→边缘盒子；USB摄像头/读卡器→录入电脑端）
CREATE TABLE devices (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_type   TINYINT      NOT NULL COMMENT '1 边缘盒子 / 2 录入电脑端 / 3 RTSP摄像头 / 4 USB摄像头 / 5 身份证读卡器',
  parent_id     BIGINT UNSIGNED DEFAULT NULL COMMENT '父级设备 ID，主设备为 NULL',
  device_key    VARCHAR(64)  DEFAULT '' COMMENT 'camera_id / usb通道 / 读卡器编号',
  name          VARCHAR(64)  NOT NULL,
  store_id      BIGINT UNSIGNED NOT NULL,
  status        TINYINT      NOT NULL DEFAULT 0 COMMENT '0 离线 / 1 在线',
  psk_hash      VARCHAR(128) DEFAULT NULL COMMENT '仅主设备',
  last_heartbeat BIGINT      DEFAULT 0,               -- Unix 秒
  config_json   JSON         DEFAULT NULL,
  created_at    BIGINT       NOT NULL,                -- Unix 秒（应用层写入）
  updated_at    BIGINT       NOT NULL,
  KEY idx_store (store_id),
  KEY idx_type (device_type),
  KEY idx_parent (parent_id),
  CONSTRAINT fk_dev_store FOREIGN KEY (store_id) REFERENCES stores(id),
  CONSTRAINT fk_dev_parent FOREIGN KEY (parent_id) REFERENCES devices(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 人员档案（敏感字段加密存储；person_type=0 顾客 / =1 内部人员）
CREATE TABLE customers (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  person_type   TINYINT      NOT NULL DEFAULT 0 COMMENT '0 顾客 / 1 内部人员(STAFF)',
  name          VARBINARY(256)  NOT NULL COMMENT 'AES-GCM 密文',
  id_card_no    VARBINARY(128)  DEFAULT NULL COMMENT 'AES-GCM 密文，仅顾客，唯一',
  staff_no      VARCHAR(32)  DEFAULT NULL COMMENT '工号，仅内部人员，唯一',
  department    VARCHAR(64)  DEFAULT '' COMMENT '部门/岗位，仅内部人员',
  gender        TINYINT      DEFAULT NULL,
  birth_date    BIGINT       DEFAULT 0,                -- Unix 秒
  address       VARBINARY(1024) DEFAULT NULL,
  id_photo_path VARCHAR(255) DEFAULT '',
  live_photo_path VARCHAR(255) DEFAULT '',
  face_feature  BLOB         DEFAULT NULL COMMENT '主特征 512*float32',
  status        TINYINT      NOT NULL DEFAULT 0 COMMENT '0 正常 / 1 黑名单 / 2 注销 / 3 离职',
  version       BIGINT       NOT NULL DEFAULT 1 COMMENT '特征版本（增量同步游标）',
  created_by    VARCHAR(64)  DEFAULT '',
  created_at    BIGINT       NOT NULL,                -- Unix 秒（应用层写入）
  updated_at    BIGINT       NOT NULL,
  UNIQUE KEY uk_card (id_card_no),
  UNIQUE KEY uk_staff (staff_no),
  KEY idx_type (person_type),
  KEY idx_status (status),
  KEY idx_version (version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 顾客附加特征（多特征支持）
CREATE TABLE customer_features (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  customer_id   BIGINT UNSIGNED NOT NULL,
  face_feature  BLOB         NOT NULL,
  source        TINYINT      NOT NULL DEFAULT 0 COMMENT '0 录入 / 1 现场补采',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  KEY idx_customer (customer_id),
  CONSTRAINT fk_cf_customer FOREIGN KEY (customer_id) REFERENCES customers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 识别记录 / 匿名轨迹（边缘盒上报）
CREATE TABLE recognition_logs (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_id     BIGINT UNSIGNED NOT NULL,
  track_id      VARCHAR(64)  NOT NULL,
  customer_id   BIGINT UNSIGNED DEFAULT NULL COMMENT '命中回填，匿名为 NULL',
  person_type   TINYINT      NOT NULL DEFAULT 0 COMMENT '0 顾客/匿名 / 1 内部人员(STAFF)',
  face_feature  BLOB         DEFAULT NULL,
  snapshot_path VARCHAR(255) DEFAULT '',
  similarity    FLOAT        DEFAULT 0,
  direction     TINYINT      DEFAULT 0 COMMENT '0 进 / 1 出',
  camera_id     VARCHAR(32)  DEFAULT '' COMMENT '多路摄像头标识（store+device+camera_no）',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  UNIQUE KEY uk_dedup (device_id, track_id, camera_id, created_at),
  KEY idx_time (created_at),
  KEY idx_customer (customer_id),
  KEY idx_type (person_type),
  KEY idx_device_time (device_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 人证核验记录
CREATE TABLE verify_records (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  customer_id   BIGINT UNSIGNED NOT NULL,
  id_card_no    VARBINARY(128)  DEFAULT NULL,
  verify_result TINYINT      NOT NULL COMMENT '0 待定 / 1 通过 / 2 不通过',
  similarity    FLOAT        DEFAULT 0,
  liveness_score FLOAT       DEFAULT 0,
  live_photo_path VARCHAR(255) DEFAULT '',
  device_id     BIGINT UNSIGNED DEFAULT NULL,
  operator      VARCHAR(64)  DEFAULT '',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  KEY idx_time (created_at),
  KEY idx_customer (customer_id),
  CONSTRAINT fk_vr_customer FOREIGN KEY (customer_id) REFERENCES customers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 来访统计（物化，回查后更新；也可实时计算不落库）
CREATE TABLE visit_stats (
  customer_id   BIGINT UNSIGNED PRIMARY KEY,
  total_visits  INT          NOT NULL DEFAULT 0 COMMENT '累计来访次数',
  visit_days    INT          NOT NULL DEFAULT 0 COMMENT '来访天数（按日去重）',
  first_visit_at BIGINT      DEFAULT 0,                -- Unix 秒
  last_visit_at  BIGINT      DEFAULT 0,                -- Unix 秒
  updated_at    BIGINT       NOT NULL,                -- Unix 秒
  CONSTRAINT fk_vs_customer FOREIGN KEY (customer_id) REFERENCES customers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 后台账号
CREATE TABLE users (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  username      VARCHAR(64)  NOT NULL UNIQUE,
  password_hash VARCHAR(128) NOT NULL,
  role          TINYINT      NOT NULL DEFAULT 1 COMMENT '0 管理员 / 1 操作员 / 2 只读',
  status        TINYINT      NOT NULL DEFAULT 1,
  created_at    BIGINT       NOT NULL                -- Unix 秒
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 操作审计日志
CREATE TABLE audit_logs (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  user_id       BIGINT UNSIGNED DEFAULT NULL,
  action        VARCHAR(64)  NOT NULL,
  target_type   VARCHAR(32)  DEFAULT '',
  target_id     BIGINT       DEFAULT NULL,
  detail        JSON         DEFAULT NULL,
  ip            VARCHAR(64)  DEFAULT '',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  KEY idx_time (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### 14.2 边缘盒子本地（SQLite）

```sql
CREATE TABLE recognition_logs (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id     INTEGER NOT NULL,
  track_id      TEXT NOT NULL,
  customer_id   INTEGER DEFAULT NULL,
  person_type   INTEGER DEFAULT 0,          -- 0 顾客/匿名 / 1 内部人员
  face_feature  BLOB DEFAULT NULL,
  snapshot_path TEXT DEFAULT '',
  similarity    REAL DEFAULT 0,
  direction     INTEGER DEFAULT 0,
  camera_id     TEXT DEFAULT '',
  created_at    INTEGER NOT NULL,          -- Unix 秒
  sync_status   INTEGER DEFAULT 0         -- 0 待上报 / 1 已上报
);
CREATE INDEX idx_type ON recognition_logs(person_type);
CREATE INDEX idx_time ON recognition_logs(created_at);
CREATE INDEX idx_sync ON recognition_logs(sync_status);

CREATE TABLE sync_cursor (
  k TEXT PRIMARY KEY,                     -- 'customers_version'
  v INTEGER DEFAULT 0
);
```

> 说明：敏感字段（身份证号、姓名、住址）在 MySQL 中以 **AES-GCM 密文**（VARBINARY）存储，应用层加解密；密钥由后台配置管理，不入库。边际盒不存身份证明文，仅存特征与抓拍；内部人员（STAFF）档案 person_type=1，同步到边缘盒时仍只下发特征 + 工号哈希（不下发明文工号/姓名）。---

## 15. 关键算法实现设计

### 15.1 人脸检测（SCRFD）

- 输入：原图（建议保持 `≤ 1280×1280`，超出等比缩放）。
- 输出：`boxes[N×4] + scores[N] + kps[N×10]`（框 + 5 关键点）。
- 后处理：
  1. 按 `det_thresh`（默认 0.5）过滤低分框；
  2. 解码 anchor → 原图坐标（模型含 stride 偏移，须按 SCRFD 原版 decode 逻辑实现）；
  3. **NMS（IoU ≥ 0.4）** 去除重叠框；
  4. 可选：`max_face_num`（默认 5）控制单帧处理上限。
- 工程要点：NMS 用循环向量化实现；多路摄像头共用推理线程池，避免重复加载模型。

### 15.2 人脸对齐（5 点仿射变换）

- 采用 ArcFace 标准目标关键点坐标（112×112 对应坐标）。
- 求仿射矩阵：`cv::estimateAffinePartial2D(src5pts, dst5pts)` → `cv::warpAffine`。
- 输出：**112×112×3 RGB**（注意 BGR→RGB 通道顺序，影响识别精度）。

### 15.3 特征提取（ArcFace）

- 预处理：像素归一化到 `[-1, 1]`（`(v - 127.5) / 128`）。
- 后处理：输出向量做 **L2 归一化**，后续相似度用内积 = 余弦相似度。
- 特征规格：`float32[512]`（2 KB），序列化时可用 base64 或二进制（二进制体积小，推荐内部接口用二进制）。

### 15.4 1:1 人证比对与阈值标定

```
cos = dot(face_feat_live, face_feat_idphoto)   // 两者均已 L2 归一化
pass = cos >= verify_thresh                      // 默认 0.50
```

- **阈值标定流程**：
  1. 采集不少于 500 组「本人现场照 vs 证件照」正样本与「不同人」负样本；
  2. 计算全部余弦分数，绘制 ROC，取 **FAR ≤ 1%** 对应的分数作为阈值；
  3. 试点门店现场复核调优（不同摄像头/光线差别较大时按门店差异化配置）。

### 15.5 1:N 检索与历史来访回查（Go 核心逻辑）

```
func HistorySearch(req SearchReq) (*VisitStats, error) {
    // 1. 召回候选：时间/门店过滤 + 分页拉取特征；仅统计顾客(person_type=0，含匿名)
    req.Scope.PersonType = 0 // 内部人员(STAFF)不参与历史来访统计
    logs := storage.QueryRecognitionLogs(req.Scope, req.StartAt, req.EndAt)

    // 2. 向量检索（内存余弦）
    type hit struct{ logID int64; sim float32 }
    hits := make([]hit, 0, len(logs))
    for _, l := range logs {
        sim := Cosine(req.FaceFeature, l.FaceFeature) // 需先归一化
        if sim >= req.Threshold {
            hits = append(hits, hit{l.ID, sim})
        }
    }

    // 3. 聚合统计（可按周/月加速：先按日粗筛）
    sort.Slice(hits, func(i, j int) bool { return hits[i].sim > hits[j].sim })
    if len(hits) > req.TopK { hits = hits[:req.TopK] }

    stats := &VisitStats{}
    daySet := map[string]struct{}{}
    for _, h := range hits {                            // h.CreatedAt 为 int64 Unix 秒
        stats.TotalVisits++
        day := time.Unix(h.CreatedAt, 0).UTC().Format("2006-01-02")
        daySet[day] = struct{}{}                        // 按天去重
        trackFirstLast(h)                               // 更新首次/最近来访
    }
    stats.VisitDays = len(daySet)
    return stats, nil
}
```

- **性能**：10 万条 × 512 维 float32 = 200 MB，纯 Go 余弦扫描约 100 ms 级。超限后切换 FAISS（`IndexFlatIP`），Go 侧经 **cgo** 调用，先 `NormalizeL2` 再 `Search`。
- **多路摄像头影响**：同一条轨迹可能被多路摄像头重复记录，回查聚合时按「同一天 + 相似特征 + 时间窗口」聚类去重后再计数，避免跨摄像头重复统计。
- **增量优化**：`recognition_logs` 按 `created_at` 分月分区；回查只扫业务需要的分区。

### 15.6 活体检测（RGB Liveness）

- 输入：对齐后的人脸图（建议与识别同一裁剪源）。
- 输出：`liveness_score ∈ [0,1]`。
- 判定：`liveness_score ≥ 0.5` 通过；录入端默认开启；可疑时提示「请眨眨眼/转头」二次确认（可选动作模式）。
- 注意：RGB 活体对低照度敏感，录入端应引导光线充足；不可用时降级为人工复核，并记录审计。

### 15.7 客流统计（跟踪 + 虚拟线判向）

- **跟踪器**：ByteTrack（适合密集人流）或 IOU Tracker（轻量）。
  - 每帧：检测框 → 与已有 track 做 IoU/ReID 关联 → 更新轨迹 → 输出稳定 `track_id`。
- **虚拟线判向**：
  - 配置虚拟线段 P1(x1,y1) → P2(x2,y2)；
  - 轨迹质心序列跨越线段时，用**跨线前后的相对位移投影**判定方向（P1→P2 法向侧）；
  - `IN += 1` / `OUT += 1`；同一 track 只计一次，进入镜头后 5 s 内不重复计（防抖动）。
- **内部人员排除**：track 若 1:N 命中 `person_type=1`（STAFF），标记 `person_type=1`，其跨越虚拟线不计入顾客客流；匿名/顾客 track 计入。支持按摄像头配置 `count_flow`。
- **去重与唯一人**：同一 track 的多次抓拍只保留质量最高的一条入库；`direction` 记录进出，`total = IN` 或按业务口径取 `IN + OUT`。
- 顶视/斜视摄像头命中率更高；出货口与人流混杂场景建议分摄像头独立虚拟线。

### 15.8 断网续传与幂等

- 边缘盒 SQLite `sync_status=0` 记录待上报；每批 200 条，成功后置 1。
- 后台唯一键 `(device_id, track_id, created_at)`：冲突时忽略并返回幂等成功。
- 补偿：心跳中携带 `pending_count`，后台可远程触发补报。

### 15.9 推理后端抽象（C++ 接口示例）

```cpp
// backend/inference_backend.h
namespace face {

struct FaceBox    { float x, y, w, h, score; std::array<float,10> kps; };
struct Feature    { std::array<float, 512> data; float l2_norm; };

class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;
    virtual std::vector<FaceBox> Detect(const cv::Mat& bgr, float thresh) = 0;
    virtual Feature Extract(const cv::Mat& aligned_rgb) = 0;          // 输入 112x112 RGB
    virtual float Liveness(const cv::Mat& aligned_rgb) = 0;
    virtual void WarmUp() = 0;
};

std::unique_ptr<IInferenceBackend> CreateOnnxBackend(const ModelPaths& paths);
std::unique_ptr<IInferenceBackend> CreateNcnnBackend(const ModelPaths& paths);
}  // namespace face
```

> 边缘盒与录入端复用同一套 `face/` 算法模块（检测/对齐/提取/活体），仅推理后端与平台编译产物不同；录入端 1:1、回查聚合由上层调用。---

## 16. 附录

### 16.1 模型清单（InsightFace 系，ONNX）

| 用途 | 模型 | 尺寸/算力档位 | 说明 |
|---|---|---|---|
| 人脸检测 | SCRFD-500M / 2.5G / 10G | 精度递升，速度递降 | 边缘盒按算力选；RK3588 建议 2.5G 或量化版 |
| 人脸识别 | ArcFace w600k_r50 / r18 / mobile | 512 维 | r50 精度最高；mobile 用于低算力 |
| 活体检测 | InsightFace 2.0 liveness | — | RGB 活体，ONNX |
| 轻量备选 | OpenCV Zoo YuNet + SFace | 极小 | 低算力/演示场景兜底 |

> 模型文件部署时放置于各端 `models/` 目录；商业部署需关注 3.4 许可证合规（官方预训练模型仅限非商业研究）。

### 16.2 主要第三方依赖清单

| 端 | 依赖 | 用途 |
|---|---|---|
| 边缘盒/录入端 | OpenCV 4.x、ONNXRuntime / NCNN、Qt6(录入端)、SQLite3、cpp-json 或 nlohmann/json、spdlog | 图像、推理、GUI、存储、日志 |
| 后台 | Go：gin、gorm、go-sqlite3/go-sql-driver/mysql、jwt、crypto、gorilla/websocket；可选 go-faiss(cgo) | API、ORM、鉴权、同步 |
| 前端 | Vite、React 18+、TypeScript 5+、Ant Design 6、axios、zustand、echarts/antd charts | 管理界面与报表 |
| 通用 | CMake、vcpkg/Conan；npm/pnpm | 构建 |

### 16.3 参考

- InsightFace：https://github.com/deepinsight/insightface
- OpenCV Zoo / HuggingFace opencv：https://huggingface.co/opencv
- ByteTrack：https://github.com/ifzhang/ByteTrack
- FAISS：https://github.com/facebookresearch/faiss
- Ant Design：https://ant.design

---

*文档版本 V1.1。后续可迭代：OpenAPI 文件落地、各端 README/开发指南、测试方案与阈值标定报告模板。*