# admin-backend 管理后台（Go）

FaceFlow · 智脸客流人证系统 —— 管理后台服务（Go + Gin + GORM）。

## 目录结构

```
admin-backend/
├── cmd/server/main.go           # 启动入口
├── internal/
│   ├── api/                     # Gin 路由与 Handler
│   │   ├── router.go            # 路由注册（/api/v1）
│   │   ├── response.go          # 统一响应体/错误码
│   │   ├── middleware.go        # 鉴权（骨架）
│   │   ├── devices.go           # 设备注册/心跳/设备树/配置
│   │   ├── customers.go         # 人员库 CRUD + 特征追加 + 增量同步
│   │   ├── records.go           # 识别记录批量上报 + 核验记录
│   │   ├── history.go           # POST /history/search
│   │   └── stats.go             # 客流/员工/单顾客统计
│   ├── config/config.go         # 环境变量配置
│   ├── security/cipher.go       # AES-256-GCM 敏感字段加解密
│   ├── search/cosine.go         # 512 维特征归一化/余弦/Top-K（FAISS 预留接口）
│   ├── service/history.go       # 历史来访回查（1:N 检索 → 聚合）
│   └── storage/                 # GORM 模型 + DB 打开
│       ├── models.go
│       └── db.go
└── migrations/schema.sql        # MySQL 生产 DDL
```

## 快速开始

```bash
cd admin-backend
go mod tidy
go build -o bin/server ./cmd/server
# 开发模式（SQLite + 不加密 + 默认 PSK）
DB_DSN="sqlite:./data/admin.db" DEVICE_PSK="dev-psk-change-me" ./bin/server
# 生产建议
# AES_KEY=<64 hex> DB_DSN="sqlite:./data/admin.db" HTTP_ADDR=":8080" ./bin/server
```

默认监听 `:8080`，健康检查：`GET /api/v1/health`。

## 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| HTTP_ADDR | :8080 | 监听地址 |
| DB_DSN | sqlite:./data/admin.db | `sqlite:` 前缀开发；MySQL 需引入 driver 后传入纯 DSN |
| AES_KEY | 空（不加密） | 32 字节 hex；生产必配，敏感字段 AES-256-GCM |
| DEVICE_PSK | dev-psk-change-me | 设备注册预共享密钥 |
| HISTORY_TOP_K | 50 | 历史回查聚合上限 |
| HISTORY_THRESHOLD | 0.40 | 历史回查相似度阈值（ArcFace 余弦） |
| RETENTION_DAYS | 365 | 识别记录留存天数（后续定时清理任务使用） |

## 已实现接口（/api/v1）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | /health | 健康检查 |
| POST | /devices/register | 设备注册（5 类；子设备带 parent_id） |
| POST | /devices/:id/heartbeat | 主设备心跳 + 子设备在线状态托管 |
| GET | /devices | 设备树查询（鉴权） |
| GET | /devices/:id/config | 拉取设备配置 |
| GET | /customers | 人员库分页查询（person_type 过滤） |
| POST | /customers | 新增人员档案；顾客录入后同步历史回查 |
| PUT/DELETE | /customers/:id | 更新 / 软删 |
| POST | /customers/:id/features | 追加特征 |
| GET | /customers/features/sync | 边缘盒增量特征拉取（内部人员仅下发工号哈希） |
| POST | /records/recognition/batch | 识别记录批量上报（幂等） |
| POST | /records/verify | 核验记录上报 |
| POST | /history/search | 历史来访回查（1:N → 聚合次数/天数/首次/最近） |
| GET | /stats/flow | 顾客客流统计 |
| GET | /stats/staff | 内部人员通行统计 |
| GET | /stats/visits/:customer_id | 单顾客来访统计 |

## 关键设计

- **历史来访回查**：`service.HistorySearch` 只统计 `person_type=0`（顾客含匿名），向量余弦线性扫描 Top-K 后按天去重聚合，并物化到 `visit_stats`；超 10 万条规模经 `search.SearchIndex` 接口切换 FAISS（cgo）。
- **设备层级**：`devices.parent_id` 自引用；子设备（RTSP/USB 摄像头、读卡器）由父设备注册与心跳托管，父离线则子树标记链路离线。
- **敏感数据**：姓名/身份证号/住址 AES-256-GCM 加密存储；内部人员同步到边缘盒仅下发工号哈希。
- **幂等**：识别记录唯一键 `(device_id, track_id, camera_id, created_at)`。

## 一键演示数据

```bash
# 1. 启动后端
./bin/server
# 2. 造演示数据（设备/历史轨迹/录入回查/员工统计）
python3 scripts/demo_seed.py
# 3. 打开前端查看
cd web && npm run dev
```

脚本验证：顾客录入后自动回查历史来访（隐去：张三录入 -> 3 次/3 天）；内部人员识别不计入顾客客流，独立统计（E1024 进 1 出 1）。

## 前端（Vite + React + TS + Ant Design 6）

```bash
cd web
npm install
npm run dev        # 开发：http://localhost:5173（被占用时自动+1）
# 生产构建
npm run build      # 产物在 web/dist/
```

- 页面：概览 / 设备树 / 人员库（顾客+内部人员录入，回显历史来访）/ 客流统计 / 员工通行 / 历史来访回查
- `vite.config.ts` 将 `/api` 代理到 Go 后端 `:8080`
- 接口封装：`src/api/`（client.ts 统一响应处理 + types.ts + index.ts）

## 待完善（骨架后续迭代）

- JWT 鉴权与设备 token 校验、RBAC
- MySQL driver 接入与连接池
- 对象存储（MinIO/S3）接管抓拍/证件照
- FAISS 索引构建任务、定时清理任务（retention）
- 客流统计按摄像头拆分、唯一人口径（跨摄像头去重）