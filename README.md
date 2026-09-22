# FaceFlow · 智脸客流人证系统

C++ / Go / React 三端一体：**边缘盒子人脸识别端 + 顾客录入电脑端 + 管理后台**，支持多路 RTSP 摄像头客流统计、身份证+人脸 1:1 核验、**顾客录入后历史来访次数回溯**、内部人员区分。

## 系统架构

```
┌─────────────────────┐      ┌──────────────────────┐
│  管理后台（Web）      │      │  顾客录入电脑端        │
│  Go + Vite+React+antd│◄────►│  C++ Qt（骨架）        │
│  JWT/RBAC/审计/报表  │ 同步 │  身份证读卡器+USB摄像头 │
└──────────┬──────────┘      └──────────┬───────────┘
           │ REST/批量上报               │ 人员库同步
           ▼                             │
┌─────────────────────┐                  │
│  边缘盒子人脸识别端    │◄─────────────────┘
│  C++：多路RTSP/检测/  │
│  客流统计/1:N识别     │
└─────────────────────┘
```

## 目录结构

| 目录 | 说明 | 状态 |
|---|---|---|
| `docs/architecture_design.md` | 架构设计文档（16 章：总体架构/算法选型/三端设计/数据/接口/流程/合规） | ✅ |
| `admin-backend/` | **Go 管理后台**：Gin + GORM，JWT 鉴权 + RBAC + 审计日志，SQLite/MySQL | ✅ 测试通过 |
| `admin-backend/web/` | **前端**：Vite + React + TypeScript + Ant Design 6（登录/设备树/人员库/客流/员工/回查/用户/审计） | ✅ |
| `admin-backend/scripts/demo_seed.py` | 一键演示数据（设备注册→历史轨迹→录入回查→统计） | ✅ |
| `edge-box/` | **边缘盒 C++ 端**：多路视频/推理后端抽象/Mock+ONNX/IOU跟踪/虚拟线客流/匿名轨迹/断网缓存 | ✅ Smoke 通过 |
| `enrollment-pc/` | **录入电脑端 C++ Qt**：读卡器抽象+模拟器/摄像头预览/1:1 核验/后台客户端 | 🚧 骨架（Qt 需目标机编译） |

## 快速开始（一键演示）

```bash
# 1. 后端（默认 :8080，管理员 admin/admin123）
cd admin-backend && go build -o bin/server ./cmd/server && ./bin/server

# 2. 演示数据（另开终端）
cd admin-backend && python3 scripts/demo_seed.py
#   预期输出：张三录入 -> 历史来访 3次/3天；顾客客流进=4；员工 E1024 进1出1

# 3. 前端（另开终端）
cd admin-backend/web && npm install && npm run dev
#   浏览器打开 http://localhost:5173 ，用 admin/admin123 登录
```

## 测试

```bash
cd admin-backend && go test ./...   # Go 单测（auth 83% / service 85% 覆盖）

cd edge-box && cmake -B build && cmake --build build && ctest --test-dir build  # C++ smoke（含 SCRFD 解码/对齐/SQLite/同步解析）

cd enrollment-pc && cmake -B build && cmake --build build && ctest --test-dir build  # 录入端纯逻辑自测（无需 Qt）
```

## 开发运行（分端详解）

> 本地演示只需「快速开始」一节；本节给三端各自的构建/运行/关键配置。

### 后端（Go，端口 :8080）

```bash
cd admin-backend
export GOPROXY=https://goproxy.cn,direct          # 国内源（可选）
go build -o bin/server ./cmd/server

# 关键环境变量（均有默认值；完整表格见 admin-backend/README.md）
export DEVICE_PSK="dev-psk-change-me"             # 设备注册/登录预共享密钥（两端需一致）
export JWT_SECRET="dev-jwt-secret-change-me"      # JWT 签名密钥（上线必改）
export TOKEN_TTL_USER_SECONDS=43200               # 用户令牌有效期秒（默认 12h）
export TOKEN_TTL_DEVICE_SECONDS=86400             # 设备令牌有效期秒（默认 24h）
./bin/server
```

- 首次启动自动迁移数据表并创建管理员（`ADMIN_USER`/`ADMIN_PASSWORD`，默认 admin/admin123）。
- **令牌机制**：登录/注册即签发 JWT（HS256 + jti），后台「令牌管理」页可查看、吊销；
  吊销后该令牌立即失效（中间件查台账拒绝）。升级到含令牌管理版本后，存量 token 需重新登录，
  边缘盒/录入端均会自动重登，无需人工干预。

### 前端（Vite，端口 :5173）

```bash
cd admin-backend/web
npm install                                   # 国内源：npm config set registry https://registry.npmmirror.com
npm run dev                                   # http://localhost:5173，admin/admin123 登录
```

### 边缘盒（C++，Mock 与真实推理）

```bash
cd edge-box
cmake -B build && cmake --build build
cp config/edge_box.json.example edge_box.json # 改 report_endpoint 指向后台（如 http://127.0.0.1:8080/api/v1）
./build/edge_box                             # 默认 Mock 推理；Web 配置界面 :8180（默认）
```

- **Mock / 真实推理如何决定**：
  - 编译期：CMake 探测到缺少 OpenCV / ONNXRuntime 依赖时，自动启用 Mock 视频源与 Mock 推理（`EDGE_BOX_MOCK_VIDEO`），用于开发/CI 验证流水线；
  - 运行期：默认 `-backend mock`；指定 `-backend onnx` 才是真实推理，onnx 后端不可用时（未编译 ORT 或模型不可用）会回退 Mock 并打日志。
  - 真机部署：安装 OpenCV + ONNXRuntime 并放置 SCRFD/ArcFace 模型后，以 `-backend onnx` 启动即走真实摄像头与推理。
- 摄像头/虚拟线/阈值为运行参数：改 `edge_box.json` 或在 Web 界面在线编辑，热重载。

### 录入端（C++ Qt / 纯逻辑）

```bash
cd enrollment-pc
cmake -B build && cmake --build build        # 无 Qt 时仅构建纯逻辑自测
ctest --test-dir build                       # logic + feature 全绿
```

Qt 应用需 Qt6（Widgets/Network/Multimedia）编译；运行前配置设备身份（不再硬编码）：

```bash
export ENROLL_API_URL="http://127.0.0.1:8080/api/v1"   # 后台地址
export ENROLL_PSK="dev-psk-change-me"                   # 与后台 DEVICE_PSK 一致
```

## 核心能力与设计要点

- **历史来访回查**（需求核心）：边缘盒保存匿名轨迹（特征+抓拍+时间）→ 顾客录入时后台 1:N 特征检索 → 聚合次数/天数/首次/最近来访（仅顾客，内部人员排除）
- **设备层级**：5 类设备（边缘盒/录入端/RTSP/USB摄像头/读卡器），RTSP→边缘盒、USB/读卡器→录入端，心跳托管子设备在线状态，后台设备树展示
- **内部人员区分**：人员库 person_type=0/1；STAFF 不计顾客客流、独立统计；同步到边缘盒仅下发工号哈希
- **开源算法**：InsightFace 系（SCRFD 检测 + ArcFace 识别 + RGB 活体），ONNX 部署，推理后端可切换（NCNN/TensorRT 预留）
- **时间字段**：数据库统一 BIGINT Unix 秒，跨 SQLite/MySQL/PostgreSQL 无方言差异
- **安全**：JWT（HS256）+ RBAC（admin/operator/viewer）+ 写操作审计 + 敏感字段 AES-256-GCM

## 环境要求

| 组件 | 要求 |
|---|---|
| Go | ≥ 1.22（国内源：`GOPROXY=https://goproxy.cn,direct`） |
| Node | ≥ 18，npm（国内源：`npm config set registry https://registry.npmmirror.com`） |
| C++（边缘盒） | CMake ≥ 3.16，可选 OpenCV 4 / ONNXRuntime / SQLite3 / cpp-httplib（third_party 已带） |
| C++（录入端） | Qt 6（Widgets/Network/Multimedia），读卡器厂商 SDK（可选） |

## 生产部署

> 完整预案见 [`docs/deployment.md`](docs/deployment.md)（MySQL/HTTPS/systemd/对象存储/上线清单）；
> 上线前必做的人脸阈值标定见 [`docs/calibration.md`](docs/calibration.md)。

**推荐拓扑（局域网门店）**：边缘盒 + 录入电脑在内网，后台单机部署（Nginx TLS → Go → MySQL）。

| 步骤 | 说明 |
|---|---|
| 1. 准备 | 域名/证书；`openssl rand -base64 48` 生成 JWT_SECRET；`openssl rand -hex 32` 生成 AES_KEY |
| 2. 数据库 | 建 MySQL 库（utf8mb4），`DB_DSN="mysql:user:pass@tcp(...)/faceflow?charset=utf8mb4"` 启动自动迁移 |
| 3. 启动 | systemd 托管（模板见 deployment.md §5），环境变量写入 `/etc/faceflow/admin.env` |
| 4. 前端 | `cd admin-backend/web && npm run build`，产物给 Nginx（SPA + 反代 /api/，见 deployment.md §4） |
| 5. 门店 | 边缘盒/录入端 systemd 托管；摄像头/虚拟线/阈值在 Web 界面或 `edge_box.json` 配置 |
| 6. 标定 | 按 calibration.md 用 30~50 人样本测定 `det/recog/verify` 阈值并下发 |
| 7. 上线检查 | 按 deployment.md §6 清单逐项核对（健康检查/设备登录/上报入库/回查/HTTPS/备份） |

- **快照存储**：局域网单机部署用默认 Local（`OBJECT_ROOT` 本地目录），**无需 MinIO/OBS**；
  多节点共享/海量场景再按 deployment.md §3 接对象存储。
- **安全底线**：`JWT_SECRET`、`AES_KEY`、`DEVICE_PSK`、`ADMIN_PASSWORD` 上线必须改；
  `AES_KEY` 只存在于环境变量，不得落入代码或数据库。

## 实现状态（Roadmap）

### ✅ 已完成

- [x] C++ 端 ONNX 推理：SCRFD 原版 anchor 解码（多 stride / [C,N]·[N,k] 布局 / NMS）、ArcFace 预处理（BGR→RGB、(v-127.5)/128、L2 归一化）、5 点相似变换对齐、RGB 活体；无第三方依赖时 Mock 回退
- [x] C++ 端断网缓存：SQLiteStore（`recognition_logs` 表 + 唯一索引 + WAL，MemStore 回退）
- [x] C++ 端批量上报：cpp-httplib + 设备登录（`POST /auth/device/login`）+ Bearer token + 401 重登重试 + RFC3339/幂等键对齐（已与 Go 后台端到端联调）
- [x] 人员库增量同步：轮询 `GET /customers/features/sync?since_version=N`，base64 解码特征、失效移除、周期调度（集成测试 `since=0 -> new=1 applied=1`）
- [x] 前端客流图表：ECharts 趋势（Dashboard 近 7 天折线 + Stats 日/周/月柱状与明细表），`npm run build` 通过
- [x] 录入电脑端骨架：C++ Qt（读卡器抽象 + 模拟器、摄像头预览、1:1 核验、后台 REST 客户端），纯逻辑单测通过（Qt 应用需目标机编译）
- [x] 生产部署预案：MySQL 驱动接入（`gorm.io/driver/mysql`）、对象存储抽象（`internal/object`，本地实现 + MinIO/S3 示例）、HTTPS/systemd 配置文档 `docs/deployment.md`

### 🚧 待部署/待真机

- [ ] ONNX 阈值/输入尺寸真机标定（默认 640/112、det 0.5 / recog 0.40 / verify 0.50）
- [x] 录入端特征提取与活体接入（ONNXRuntime，src/feature/，与 edge-box 同向量空间）
- [ ] 华视/精伦读卡器 SDK 适配（enrollment-pc，需厂商 SDK）
- [x] 边缘盒多路线程池（文档 4.1.6）与抓拍图上报（base64 → 后台）
- [x] 快照存储抽象 `internal/object`（默认 Local 本地磁盘；MinIO/OBS 为可选扩展，**局域网部署无需**）
- [ ] 生产环境实际部署（MySQL/HTTPS 按 `docs/deployment.md` 执行；对象存储按需选用 Local 或 MinIO）

## 参考

- 架构设计文档：`docs/architecture_design.md`
- 生产部署预案：`docs/deployment.md`
- 人脸阈值现场标定：`docs/calibration.md`
- InsightFace：https://github.com/deepinsight/insightface
- Ant Design 6：https://ant.design