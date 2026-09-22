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
- [ ] 录入端特征提取与活体接入（ONNXRuntime），华视/精伦读卡器 SDK 适配
- [ ] 边缘盒抓拍图上传（对象存储）与多路线程池（文档 4.1.6）
- [ ] 生产环境实际部署（MySQL/MinIO/HTTPS 按 `docs/deployment.md` 执行）

## 参考

- 架构设计文档：`docs/architecture_design.md`
- InsightFace：https://github.com/deepinsight/insightface
- Ant Design 6：https://ant.design