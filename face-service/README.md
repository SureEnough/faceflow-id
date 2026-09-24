# FaceFlow face-service（Python 人脸特征服务）

给管理后台「人员管理-手动添加 / 模板导入」提供 **头像 → 人脸特征** 提取能力。
特征向量空间与边缘盒 C++ 端一致（512 维 float32、L2 归一化、ArcFace 预处理），
提取到的特征可直接入库并通过 `GET /customers/features/sync` 下发给边缘盒做 1:N 识别。

## 快速开始（Mock 联调模式，无需模型）

```bash
cd face-service
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

export FACE_SERVICE_MOCK=1        # 无模型，返回确定性伪特征（同一图片结果稳定）
python3 -m uvicorn app:app --host 0.0.0.0 --port 8090
```

验证：

```bash
curl -s http://127.0.0.1:8090/api/health
# {"status":"ok","engine":"mock","dim":512,"api_key_required":false}

curl -s -X POST http://127.0.0.1:8090/api/face/extract \
  -F "image=@/path/to/photo.jpg"
# {"code":0,"message":"ok","data":{"feature_b64":"...","dim":512,"faces":1,"engine":"mock"}}
```

## 真实推理（ONNX）

放置模型到 `face-service/models/`（同名），或通过环境变量指定路径：

| 环境变量 | 默认 | 说明 |
|---|---|---|
| `FACE_SERVICE_DET_MODEL` | `models/det_10g.onnx` | SCRFD 检测模型（与边缘盒一致） |
| `FACE_SERVICE_REC_MODEL` | `models/w600k_r50.onnx` | ArcFace 特征模型（512 维） |
| `FACE_SERVICE_MOCK` | 空 | `1` 时强制 Mock（不加载模型） |
| `FACE_SERVICE_API_KEY` | 空 | 非空时要求请求头 `X-API-Key` 匹配 |
| `FACE_SERVICE_PORT` | 8090 | `python3 app.py` 直接运行时监听端口 |

> 模型可从 InsightFace 官方仓库（w600k_r50 / det_10g）或边缘盒部署包获取，
> 需与边缘盒使用同一套模型，保证特征向量空间一致。

## 接口契约

### `GET /api/health`

```json
{"status":"ok","engine":"mock|arcface","dim":512,"api_key_required":false}
```

### `POST /api/face/extract`

multipart 表单，字段 `image`（文件）。成功：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "feature_b64": "<base64 512*float32, little-endian>",
    "dim": 512,
    "faces": 1,
    "engine": "mock"
  }
}
```

失败：`400` 空图片 / 参数错误；`404` 未检测到人脸或图片解码失败；`401` API Key 错误。

### `POST /api/face/extract_json`

JSON `{"image_b64": "<base64 图片>"}`，返回同上（供不方便上传文件的调用方）。

## 测试

```bash
cd face-service
FACE_SERVICE_MOCK=1 python3 -m pytest test_app.py -v
```