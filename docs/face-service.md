# face-service（人脸特征服务）与人员头像人脸识别

本文档说明 **face-service**（Python 实现的人脸识别特征服务）的部署与使用，
以及管理后台如何通过它对手动添加 / 模板导入的人员头像自动做**人脸识别（特征提取）**，
实现"上传照片即建档、可下发边缘盒做 1:N 识别"。

## 1. 整体链路

```
管理后台（人员管理）
  ├─ 手动添加：上传头像 → POST /customers  → 后台调 face-service 提取 512 维特征 → 入库
  └─ 模板导入：Excel（头像内嵌图片）→ POST /customers/import → 逐行调 face-service → 入库

face-service（Python FastAPI，默认 :8090）
  POST /api/face/extract  图片 → {feature_b64: 512 维 float32 特征}
  引擎：SCRFD 检测 + ArcFace 特征（ONNX），与边缘盒 C++ 端同一向量空间；
       无模型时 FACE_SERVICE_MOCK=1 返回确定性伪特征，便于联调。
```

- 特征入库后，边缘盒通过 `GET /customers/features/sync` 增量拉取，即可离线 1:N 识别。
- 未配置 face-service 地址时，管理后台回退到默认地址 `http://127.0.0.1:8090`
  （可用环境变量 `FACE_SERVICE_URL` 覆写默认值）。

## 2. face-service 部署

### 2.1 依赖与启动

```bash
cd face-service
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

# 联调模式（无模型，返回确定性特征）
FACE_SERVICE_MOCK=1 python3 -m uvicorn app:app --host 0.0.0.0 --port 8090

# 真实推理（需放置模型）
#   models/det_10g.onnx   SCRFD 检测模型
#   models/w600k_r50.onnx ArcFace 特征模型（512 维）
python3 -m uvicorn app:app --host 0.0.0.0 --port 8090
```

环境变量：

| 变量 | 默认 | 说明 |
|---|---|---|
| `FACE_SERVICE_MOCK` | 空 | `1` 时强制 Mock，无需模型 |
| `FACE_SERVICE_DET_MODEL` | `models/det_10g.onnx` | 检测模型路径 |
| `FACE_SERVICE_REC_MODEL` | `models/w600k_r50.onnx` | 特征模型路径 |
| `FACE_SERVICE_API_KEY` | 空 | 非空时要求请求头 `X-API-Key` 匹配 |
| `FACE_SERVICE_PORT` | 8090 | `python3 app.py` 直接运行时监听端口 |

### 2.2 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/health` | 健康检查，返回引擎模式 |
| POST | `/api/face/extract` | multipart 上传 `image` 文件，返回特征 |
| POST | `/api/face/extract_json` | JSON `{"image_b64": "..."}` 返回特征 |

成功响应：

```json
{"code":0,"message":"ok","data":{"feature_b64":"<base64 512*float32>","dim":512,"faces":1,"engine":"mock"}}
```

失败：`400` 空图片/参数；`404` 未检测到人脸或图片损坏；`401` API Key 错误。

### 2.3 模型与向量空间

- 必须与边缘盒使用**同一套模型**（SCRFD + ArcFace，输出 512 维，L2 归一化，
  预处理 `BGR→RGB、(v-127.5)/128`），否则提取的特征与边缘盒不在同一空间，1:N 无法命中。
- 5 点关键点对齐到 112×112（ArcFace 标准 align，与 edge-box `face_align.cpp` 一致）。

## 3. 管理后台配置

系统配置入口：管理后台 → **系统管理 → 系统配置**（仅 admin）。

| 配置项 | 说明 |
|---|---|
| 人脸识别服务地址（face_service_url） | 留空=使用默认 `http://127.0.0.1:8090`；可填独立 face-service 或**边缘盒**地址 |
| 人脸识别服务密钥（face_service_key） | 两种鉴权：纯密钥→ `X-API-Key` 头（face-service）；`user:pass` 形式→ **HTTP Basic Auth**（边缘盒 Web 界面方式，如 `admin:密码`） |

### 3.1 两种接入方式

- **独立 face-service（推荐）**：地址填 `http://<face-service>:8090`，密钥填其 `FACE_SERVICE_API_KEY`；
- **直接对接边缘盒**：地址填 `http://<edge-box>:8180`，密钥填 `admin:边缘盒web密码`（Basic Auth）。
  边缘盒需为包含 `/api/face/extract` 接口的版本；无摄像头/无 OpenCV 时也可用 Mock 后端联调。

- 配置保存在 `system_configs` 表，运行期在线生效，无需重启。
- 也支持环境变量作为默认值：`FACE_SERVICE_URL` / `FACE_SERVICE_KEY`
  （系统配置表中保存的值优先于环境变量）。

接口（均为 admin）：

```
GET /api/v1/system/config   读取配置（密钥不回传明文，仅返回是否已配置）
PUT /api/v1/system/config   保存配置（face_service_url 空=恢复默认；key 空/*=不修改）
```

## 4. 手动添加人员（自动提取特征）

管理后台 → 人员管理 → **新增人员**：

1. 填写类型/姓名/证件等；
2. **上传头像**（正脸照片，jpg/png/bmp），上传后弹出**自定义裁剪**（拖拽/缩放/旋转，默认 1:1，输出 512×512）；
3. 可选：不填"人脸特征"（系统自动调 face-service 提取）；也可手动粘贴特征。

行为：

- 上传了头像但未填特征：后端自动调 face-service，提取失败（无脸/服务不可用）会明确报错；
- 头像与特征均未填：仍可建档（无识别能力，兼容人工补特征场景）。

## 5. 模板导入（Excel，头像内嵌图片）

入口：人员管理 → **模板导入 → 下载导入模板**。

### 5.1 模板格式

- Sheet「人员档案」表头固定（顺序可调，表头文字一致即可）：

  | 人员类型 | 姓名 | 身份证号 | 工号 | 部门 | 性别 | 出生日期 | 住址 | 门店 | 状态 | 头像 | 附加照片1 | 附加照片2 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|

- **门店**：填门店名称。已存在则自动关联；不存在自动创建（状态=正常）；留空=未分配。
- **状态**（可空）：正常 / 黑名单 / 注销 / 离职，留空=正常。
- **照片列**（Excel `插入 → 图片` 嵌入对应单元格，jpg/png/bmp，单行单张）：
  - `头像`：主照片 → 提取主特征（512 维）入库；
  - `附加照片1/2`：可选，分别提取为**附加特征**（`customer_features` 表，多照片提升识别率）；
  - 主照片提取失败 → 该行失败；附加照片提取失败 → 仅跳过该张，不影响整行。
- Sheet「填写说明」含完整填写指引与示例（示例行含门店/多照片演示）。

### 5.2 导入规则

- `POST /api/v1/customers/import`（admin/operator），multipart 字段 `file`，≤10MB；
- 逐行处理，失败行不中断，返回成功/失败明细（行号 + 姓名 + 原因）；
- 校验：顾客必填身份证号；内部人员必填工号；身份证/工号唯一冲突会标记该行失败；
- 门店按名称自动创建/关联；状态支持 正常/黑名单/注销/离职；
- 无法识别到人脸的头像 → 该行标记失败（原因：头像中未检测到人脸）；
- face-service 未配置/不可用 → 所有带头人像的行标记失败（原因：人脸识别服务不可用）。

响应示例：

```json
{"code":0,"message":"ok","data":{"total":3,"success":2,"failed":[{"row":4,"name":"张三","reason":"头像中未检测到人脸"}]}}
```

## 6. 测试

```bash
# face-service
cd face-service && FACE_SERVICE_MOCK=1 python3 -m pytest test_app.py -v

# 管理后台（含导入/系统配置/faceservice 客户端单测）
cd admin-backend && go test ./...
```

## 7. 常见问题

| 现象 | 处理 |
|---|---|
| 手动添加/导入报"人脸识别服务不可用" | 检查该地址服务是否启动（face-service `curl :8090/api/health`；边缘盒 `curl -u admin:密码 :8180/api/status`）、系统配置的地址/密钥是否正确 |
| 上传照片报"未检测到人脸" | 换清晰正脸照片；确认 face-service 为真实 ONNX 引擎且模型正常 |
| 导入后边缘盒识别不到新人员 | 特征向量空间不一致（换回与边缘盒同套模型）；或等待边缘盒增量同步（`since_version` 轮询） |