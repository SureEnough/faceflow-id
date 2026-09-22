# FaceFlow 生产部署预案（roadmap #5）

> 目标环境：边缘盒（RK3588 / Jetson / x86 工控机）+ 门店录入电脑 + 云端/机房管理后台。
> 本文覆盖：MySQL 接入、对象存储（MinIO/S3）、HTTPS、进程托管、上线检查。实际部署需在用户环境执行。

## 1. 架构拓扑

```
                    ┌──────────────────────────── 云端/机房 ────────────────────────────┐
                    │  Nginx(:443, TLS) ──► admin-backend(Go) ──► MySQL 8.0             │
                    │        │                     │                                    │
                    │        └──► /static/* ──►  MinIO / S3 (快照/照片)                │
                    └───────────────────────────────────────────────────────────────────┘
  边缘盒 ──HTTP(S)──┐   认证: /auth/device/login (psk→JWT, dev claim)
  录入电脑 ─HTTP(S)──┘   上报: POST /records/recognition/batch（幂等键 device+track+camera+time）
                        同步: GET  /customers/features/sync?since_version=N
```

- 边缘盒与录入电脑**不暴露公网端口**，仅主动出站连云端（也可 NAT/专线）。
- 摄像头（RTSP）不出门店；快照经边缘盒上报，转存对象存储。

## 2. MySQL 接入

代码已支持（`internal/storage/db.go`，DSN 前缀 `mysql:`，依赖 `gorm.io/driver/mysql`）。

```bash
# 1) 建库（utf8mb4，避免 emoji/姓名生僻字乱码）
CREATE DATABASE faceflow DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'faceflow'@'%' IDENTIFIED BY '<强密码>';
GRANT ALL PRIVILEGES ON faceflow.* TO 'faceflow'@'%';
FLUSH PRIVILEGES;

# 2) 启动时自动迁移（AutoMigrate；生产建议先备份+灰度）
DB_DSN="mysql:faceflow:<强密码>@tcp(mysql.internal:3306)/faceflow?charset=utf8mb4&parseTime=True&loc=Local" \
./bin/server
```

- 时间字段统一 BIGINT Unix 秒，SQLite/MySQL 无方言差异（`stats.go` 已按 dialect 切换 `strftime`/`FROM_UNIXTIME`）。
- 连接参数建议：`&charset=utf8mb4&parseTime=True&loc=Local`；生产可加 `timeout=5s&readTimeout=10s&writeTimeout=10s&maxAllowedPacket=64<<20`。
- 敏感字段（身份证号/姓名/住址）应用层 AES-256-GCM 加密入库（`security.Cipher`），密钥经 `AES_KEY` 环境变量注入，**不得入库**。

## 3. 对象存储（抓拍图/证件照）

已实现本地磁盘实现（`internal/object`，`OBJECT_ROOT` + `OBJECT_PUBLIC_URL`），满足单机演示：

```bash
OBJECT_ROOT=./data/objects OBJECT_PUBLIC_URL=https://static.example.com/ ./bin/server
```

### 生产切换 MinIO / S3

`Storage` 接口已就绪，新增实现即可（示例 minio-go）：

```go
// internal/object/minio.go（生产接入示例）
import "github.com/minio/minio-go/v7"

type minioStore struct{ cli *minio.Client; bucket string; publicURL string }
func (m *minioStore) Put(ctx context.Context, key string, r io.Reader, ct string) error {
    _, err := m.cli.PutObject(ctx, m.bucket, key, r, -1,
        minio.PutObjectOptions{ContentType: ct})
    return err
}
func (m *minioStore) Get(ctx context.Context, key string) (io.ReadCloser, error) {
    o, err := m.cli.GetObject(ctx, m.bucket, key, minio.GetObjectOptions{})
    if err != nil { return nil, err }
    if _, err := o.Stat(); err != nil { o.Close(); return nil, object.ErrNotFound }
    return o, nil
}
func (m *minioStore) Delete(ctx context.Context, key string) error {
    return m.cli.RemoveObject(ctx, m.bucket, key, minio.RemoveObjectOptions{})
}
func (m *minioStore) URL(ctx context.Context, key string) string {
    return m.publicURL + "/" + key // 桶前缀 + CDN
}
func (m *minioStore) Kind() string { return "minio" }
```

- 上报接口对 `snapshot`（base64）自动转存（`records.go`），库存对象 key；读取端经 `URL(key)` 取图。
- MinIO 建议内网部署 + 桶策略私有，前端经后台代理或 CDN Signed URL 访问。

## 4. HTTPS / TLS

两种方式（二选一）：

### 4.1 Nginx 反代终止 TLS（推荐）

```nginx
server {
    listen 443 ssl http2;
    server_name admin.example.com;
    ssl_certificate     /etc/letsencrypt/live/admin.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/admin.example.com/privkey.pem;

    client_max_body_size 20m;              # 抓拍图上限

    location /api/ {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
    location / {
        root /opt/faceflow/admin-backend/web/dist;   # vite build 产物
        try_files $uri $uri/ /index.html;
    }
}
server { listen 80; server_name admin.example.com; return 301 https://$host$request_uri; }
```

### 4.2 Gin 直启 TLS（边缘场景，证书放本机）

```go
if err := r.RunTLS(cfg.HTTPAddr, "/etc/ssl/faceflow.crt", "/etc/ssl/faceflow.key"); err != nil { ... }
```

边缘盒上报地址改为 `https://admin.example.com/api/v1`（C++ httplib 支持 HTTPS，链证书注意自签 CA）。

## 5. 进程托管与运维

### systemd（admin-backend）

```ini
[Unit]
Description=FaceFlow Admin Backend
After=network.target mysql.service

[Service]
User=faceflow
WorkingDirectory=/opt/faceflow/admin-backend
EnvironmentFile=/etc/faceflow/admin.env
ExecStart=/opt/faceflow/admin-backend/bin/server
Restart=always
RestartSec=3
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

`/etc/faceflow/admin.env` 示例：

```bash
HTTP_ADDR=:8080
DB_DSN=mysql:faceflow:********@tcp(127.0.0.1:3306)/faceflow?charset=utf8mb4&parseTime=True&loc=Local
DEVICE_PSK=********          # 与门店边缘盒/录入端一致
JWT_SECRET=****************  # openssl rand -base64 48
AES_KEY=****************      # 32 字节 hex: openssl rand -hex 32
ADMIN_USER=admin
ADMIN_PASSWORD=********
OBJECT_ROOT=/opt/faceflow/data/objects
OBJECT_PUBLIC_URL=https://static.example.com/
RETENTION_DAYS=365
```

### 边缘盒

- 用 systemd 或 kontain 托管 `edge_box`，`Restart=always`；
- 日志 `journalctl -u edge_box`；建议 syslog 转发到集中日志（ELK/Loki）；
- 部署前把 `config/edge_box.json.example` 改造成门店配置（摄像头 URL、虚拟线、阈值）。

## 6. 上线检查清单

- [ ] MySQL 迁移完成，`GET /api/v1/health` 返回 up
- [ ] 设备注册 + 设备登录（`/auth/device/login`，正确/错误 psk 各验一次）
- [ ] 边缘盒启动日志出现 `features sync: since=0 -> new=N`，且门店人员识别命中
- [ ] 上报入库：后台 `recognition_logs` 出现匿名（customer_id NULL）与已识别记录
- [ ] 历史回查：录入顾客后 `/history/search` 返回来访聚合
- [ ] 前端 `npm run build` 产物经 Nginx 可登录、图表有数据
- [ ] HTTPS 证书链正常；边缘盒/录入端能访问 `https://admin.example.com`
- [ ] 对象存储：抓拍图可访问；MinIO/S3 bucket 权限为私有
- [ ] 权限：删除/用户管理等接口仅 admin；审计日志有写操作记录
- [ ] 备份：MySQL 每日备份（mysqldump）+ 对象存储跨区复制（如有合规要求）