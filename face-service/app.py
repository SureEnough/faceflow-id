"""FaceFlow face-service —— 人脸特征提取 HTTP 服务。

供管理后台（人员管理-手动添加/模板导入）对头像图片做人脸识别，返回
与边缘盒子同向量空间的 512 维特征。

接口：
- GET  /api/health       健康检查
- POST /api/face/extract 提取人脸特征（multipart）

鉴权：请求头 X-API-Key。环境变量 FACE_SERVICE_API_KEY 未设置时不校验（开发模式）。
运行：python3 -m uvicorn app:app --host 0.0.0.0 --port 8090
"""

from __future__ import annotations

import base64
import os

import numpy as np
from fastapi import FastAPI, File, Header, UploadFile
from fastapi.responses import JSONResponse
from pydantic import BaseModel

from face_engine import DIM, create_engine, feature_to_b64

app = FastAPI(title="FaceFlow face-service", version="1.0.0")
engine = create_engine()

API_KEY = os.getenv("FACE_SERVICE_API_KEY", "").strip()


def _ok(data):
    return {"code": 0, "message": "ok", "data": data}


def _err(code: int, message: str, http_status: int):
    return JSONResponse(status_code=http_status, content={"code": code, "message": message})


def _check_auth(x_api_key: str | None):
    if API_KEY and x_api_key != API_KEY:
        return _err(401, "invalid api key", 401)
    return None


class ExtractBody(BaseModel):
    image_b64: str  # base64 图片（jpg/png/bmp）


def _extract(image_bytes: bytes):
    if not image_bytes:
        return _err(400, "empty image", 400)
    result = engine.extract(image_bytes)
    if result is None:
        # 图片解码失败 / 未检测到人脸（Mock 模式对任意非空字节都返回特征）
        return _err(404, "no face detected or image decode failed", 404)
    feat, faces = result
    return _ok({"feature_b64": feature_to_b64(feat), "dim": DIM, "faces": faces, "engine": engine.name})


@app.get("/api/health")
def health():
    return _ok({"status": "ok", "engine": engine.name, "dim": DIM, "api_key_required": bool(API_KEY)})


@app.post("/api/face/extract")
async def extract_multipart(
    image: UploadFile = File(..., description="头像图片文件（jpg/png/bmp/jpeg/webp）"),
    x_api_key: str | None = Header(default=None, alias="X-API-Key"),
):
    auth_err = _check_auth(x_api_key)
    if auth_err is not None:
        return auth_err
    data = await image.read()
    return _extract(data)


@app.post("/api/face/extract_json")
async def extract_json(body: ExtractBody, x_api_key: str | None = Header(default=None, alias="X-API-Key")):
    auth_err = _check_auth(x_api_key)
    if auth_err is not None:
        return auth_err
    try:
        data = base64.b64decode(body.image_b64)
    except Exception:
        return _err(400, "image_b64 invalid", 400)
    return _extract(data)


if __name__ == "__main__":
    import uvicorn

    port = int(os.getenv("FACE_SERVICE_PORT", "8090"))
    uvicorn.run(app, host="0.0.0.0", port=port)