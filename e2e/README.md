# FaceFlow 统一 E2E（Playwright）

覆盖两个 Web 前端，各自自动拉起后端依赖：

| 项目 | 前端 | 后端依赖 | 端口 |
|---|---|---|---|
| `edge-box` | 边缘盒 Web 配置界面（React+antd6） | C++ `edge_box`（mock 后端） | 18191 |
| `admin` | 管理后台（React+antd6） | Go `bin/server` + `vite preview`（反代 `/api`） | 18192 / 18089 |

## 前置构建（一次性）

```bash
# C++ 边缘盒服务
cd edge-box && cmake -B build && cmake --build build
# 边缘盒前端
cd edge-box/web && npm install && npm run build

# Go 后台
cd admin-backend && go build -o bin/server ./cmd/server
# 管理后台前端
cd admin-backend/web && npm install && npm run build
```

## 安装与运行

```bash
cd e2e
npm install
# 首次下载 Chromium（国内镜像加速）
PLAYWRIGHT_DOWNLOAD_HOST=https://npmmirror.com/mirrors/playwright npm run e2e:install

# 全部项目
npm run e2e
# 单项目
npm run e2e:edge
npm run e2e:admin
```

## 常用命令

| 命令 | 说明 |
|---|---|
| `npm run e2e` | 跑全部项目 |
| `npm run e2e:edge` / `e2e:admin` | 跑单项目 |
| `npm run e2e:install` | 安装 Chromium |
| `npm run e2e:report` | 打开 HTML 报告 |
| `npm run e2e:open` | UI 模式调试 |
| `DEBUG=1 npm run e2e` | 显示被拉起服务的 stdout |
| `EDGE_BOX_BIN=/path/edge_box npm run e2e:edge` | 指定边缘盒二进制 |
| `E2E_API_TARGET=http://host:port npm run e2e:admin` | 指定 admin 后端地址 |

## 目录

```
e2e/
├── package.json              # @playwright/test 1.40.0 + npm scripts
├── playwright.config.ts      # projects: edge-box / admin（各自 webServer）
├── helpers/
│   ├── start-edge-box.mjs    # 拉起 edge_box（fixture 见 tests/edge-box/edge_box.e2e.json）
│   └── start-admin.mjs       # backend: Go bin/server；preview: vite preview 反代
├── tests/
│   ├── edge-box/app.spec.ts  # 登录/状态/摄像头/参数/API（7 条）
│   └── admin/app.spec.ts     # 登录/仪表盘/设备下发/客流空态/API（7 条）
└── .runtime/                 # 运行期数据（gitignore）
```

## 说明

- 端口/账号密码以各 fixture 为准（edge-box: `admin/e2e-pass`；admin: `admin/admin123`，由 `start-admin.mjs` 注入环境变量）。
- `reuseExistingServer`：本地已跑着同端口服务时直接复用（CI 强制新建）。
- admin 前端经 `vite preview` 提供，`/api` 由 preview 代理到 Go 后端；后端环境变量在 `start-admin.mjs` 内固定，保证可复现。
- 运行期 SQLite/日志写入 `e2e/.runtime/` 与测试目录（均已 gitignore）。