"""face-service 接口测试（Mock 引擎下运行：FACE_SERVICE_MOCK=1）。"""

import base64

import numpy as np
import pytest
from fastapi.testclient import TestClient

from app import app

client = TestClient(app)


def make_png() -> bytes:
    """1x1 红色 PNG"""
    return (
        b"\x89PNG\r\n\x1a\n"
        + b"\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90wS\xde"
        + b"\x00\x00\x00\x0cIDATx\x9cc\xf8\xff\xff?\x00\x05\xfe\x02\xfe\xa7\xb5\x8b\xf8\x00\x00\x00\x00IEND\xaeB`\x82"
    )


def engine_name() -> str:
    return client.get("/api/health").json()["data"]["engine"]


def test_health():
    r = client.get("/api/health")
    assert r.status_code == 200
    d = r.json()["data"]
    assert d["status"] == "ok"
    assert d["dim"] == 512


def test_extract_multipart_deterministic():
    png = make_png()
    r1 = client.post("/api/face/extract", files={"image": ("a.png", png, "image/png")})
    r2 = client.post("/api/face/extract", files={"image": ("a.png", png, "image/png")})
    assert r1.status_code == 200
    assert r1.json()["code"] == 0
    data = r1.json()["data"]
    assert data["dim"] == 512
    feat = np.frombuffer(base64.b64decode(data["feature_b64"]), dtype="<f4")
    assert feat.shape == (512,)
    # 确定性：同图同特征
    assert r1.json()["data"]["feature_b64"] == r2.json()["data"]["feature_b64"]
    # L2 归一化
    assert abs(float(np.linalg.norm(feat)) - 1.0) < 1e-3


def test_extract_empty():
    r = client.post("/api/face/extract", files={"image": ("e.bin", b"", "application/octet-stream")})
    assert r.status_code == 400


def test_extract_not_image():
    r = client.post("/api/face/extract", files={"image": ("e.bin", b"not-an-image", "image/png")})
    # Mock 引擎对任意非空字节返回特征（联调宽松行为）；ONNX 引擎返回 404
    if engine_name() == "mock":
        assert r.status_code == 200
    else:
        assert r.status_code == 404


def test_extract_json():
    png = make_png()
    r = client.post("/api/face/extract_json", json={"image_b64": base64.b64encode(png).decode()})
    assert r.status_code == 200
    assert r.json()["data"]["dim"] == 512


def test_extract_json_invalid_b64():
    r = client.post("/api/face/extract_json", json={"image_b64": "!!!"})
    assert r.status_code == 400


def test_api_key():
    import os

    if not os.getenv("FACE_SERVICE_API_KEY"):
        pytest.skip("FACE_SERVICE_API_KEY 未设置")
    r = client.post("/api/face/extract", files={"image": ("a.png", make_png(), "image/png")})
    assert r.status_code == 401


if __name__ == "__main__":
    pytest.main([__file__, "-v"])