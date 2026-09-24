"""FaceFlow face-service 人脸特征引擎。

支持两种后端：
- OnnxEngine：SCRFD 人脸检测 + ArcFace 特征提取（ONNXRuntime，与 edge-box C++
  端同向量空间：512 维 float32，L2 归一化，5 点相似变换对齐到 112x112）。
- MockEngine：不依赖任何模型。按图片内容哈希生成确定性 512 维特征，
  用于前后端联调 / 无 GPU 环境演示（同一张图结果稳定，不同图结果不同）。

选择逻辑：
- 环境变量 FACE_SERVICE_MOCK=1 时强制 Mock；
- 否则检测 models/ 目录下模型文件（或 FACE_SERVICE_DET_MODEL /
  FACE_SERVICE_REC_MODEL 显式指定），存在则用 OnnxEngine，缺失则回退 Mock
  并打印告警。
"""

from __future__ import annotations

import base64
import hashlib
import os
import struct
from typing import List, Optional, Tuple

import numpy as np

DIM = 512  # ArcFace 输出维度，与后端 search.Dim 一致

# ArcFace 标准对齐 5 点目标（112x112 输入空间）
ARCFACE_DST = np.array(
    [
        [38.2946, 51.6963],
        [73.5318, 51.5014],
        [56.0252, 71.7366],
        [41.5493, 92.3655],
        [70.7299, 92.2041],
    ],
    dtype=np.float32,
)

# SCRFD 检测输入尺寸与 stride（det_10g 等默认 640）
DET_INPUT_SIZE = 640
DET_STRIDES = [8, 16, 32]
DET_SCORE_THRESH = 0.5
DET_NMS_THRESH = 0.4


def l2_normalize(feat: np.ndarray) -> np.ndarray:
    norm = np.linalg.norm(feat)
    if norm < 1e-12:
        return feat
    return feat / norm


def feature_to_b64(feat: np.ndarray) -> str:
    """512 维 float32 -> base64（little-endian，与 Go 端 decodeFeature 对齐）。"""
    return base64.b64encode(feat.astype("<f4").tobytes()).decode("ascii")


def deterministic_feature(data: bytes) -> np.ndarray:
    """由图片字节生成确定性 512 维特征（Mock 用）：sha256 -> 伪随机 -> L2。"""
    h = hashlib.sha256(data).digest()
    # 用 64 字节种子扩展出 512 个 float32
    rng = hashlib.sha512(h).digest()
    out = np.frombuffer(rng, dtype=np.uint16).astype(np.float32)[:DIM]
    while len(out) < DIM:  # 理论不会发生（sha512=64B -> 32 个 uint16），防御性补齐
        h = hashlib.sha256(h + bytes([0])).digest()
        extra = np.frombuffer(rng + h, dtype=np.uint16).astype(np.float32)
        out = np.concatenate([out, extra])
    out = out[:DIM]
    return l2_normalize(out - np.mean(out))


class MockEngine:
    """无模型联调引擎。"""

    name = "mock"

    def extract(self, image_bytes: bytes) -> Optional[Tuple[np.ndarray, int]]:
        if not image_bytes:
            return None
        return deterministic_feature(image_bytes), 1


class OnnxEngine:
    """SCRFD + ArcFace 推理引擎。"""

    name = "arcface"

    def __init__(self, det_model: str, rec_model: str):
        import cv2  # noqa: F401  确保 cv2 可用
        import onnxruntime as ort

        self._ort = ort
        self._cv2 = cv2
        self.det_session = ort.InferenceSession(det_model, providers=self._providers())
        self.rec_session = ort.InferenceSession(rec_model, providers=self._providers())
        # 输入名（SCRFD 通常为 input.1 / input；ArcFace 通常为 data/input）
        self.det_input = self.det_session.get_inputs()[0].name
        self.rec_input = self.rec_session.get_inputs()[0].name
        self.det_input_size = DET_INPUT_SIZE

    @staticmethod
    def _providers():
        try:
            import onnxruntime as ort

            return ort.get_available_providers()
        except Exception:
            return ["CPUExecutionProvider"]

    # ---------- 检测 ----------
    def _detect(self, img_bgr: np.ndarray) -> List[dict]:
        cv2 = self._cv2
        h, w = img_bgr.shape[:2]
        scale = self.det_input_size / max(h, w)
        rh, rw = int(round(h * scale)), int(round(w * scale))
        resized = cv2.resize(img_bgr, (rw, rh))
        blob = np.zeros((1, 3, self.det_input_size, self.det_input_size), dtype=np.float32)
        rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
        blob[0, :, :rh, :rw] = (rgb.transpose(2, 0, 1).astype(np.float32) / 255.0)
        try:
            outputs = self.det_session.run(None, {self.det_input: blob})
        except Exception:
            return []

        # SCRFD decode：outputs 前 9 个为 [scores(3), bboxes(3), kps(3*2=6)] 按 stride 排列
        if len(outputs) < 9:
            return []
        scores_all, bboxes_all, kps_all = [], [], []
        for i, stride in enumerate(DET_STRIDES):
            scores_all.append(outputs[i])
            bboxes_all.append(outputs[i + 3])
            kps_all.append(outputs[i + 6])
        scores = np.concatenate(scores_all, axis=1)[0]  # (1, N, 1) -> (N,)
        bboxes = np.concatenate(bboxes_all, axis=1)[0]
        kps = np.concatenate(kps_all, axis=1)[0]
        # 汇总各 stride 的 anchor 中心
        centers = []
        for stride in DET_STRIDES:
            fh, fw = self.det_input_size // stride, self.det_input_size // stride
            yy, xx = np.meshgrid(np.arange(fh), np.arange(fw), indexing="ij")
            centers.append(np.stack([xx.ravel(), yy.ravel()], axis=1) * stride)
        centers = np.concatenate(centers, axis=0)

        idx = np.where(scores > DET_SCORE_THRESH)[0]
        boxes = []
        for i in idx:
            cx, cy = centers[i]
            bw, bh = bboxes[i][2], bboxes[i][3]
            x1, y1 = cx - bw / 2.0, cy - bh / 2.0
            k = kps[i].reshape(5, 2) * stride  # 关键点需按对应 stride 缩放
            k = k * scale  # 统一缩放到原图坐标
            boxes.append(
                {
                    "score": float(scores[i]),
                    "box": [x1 / scale, y1 / scale, (x1 + bw) / scale, (y1 + bh) / scale],
                    "kps": k.tolist(),
                }
            )
        if not boxes:
            return []

        # 按 score 排序 + NMS
        boxes.sort(key=lambda b: b["score"], reverse=True)
        keep = []
        for b in boxes:
            x1, y1, x2, y2 = b["box"]
            if any(self._iou(b["box"], kb["box"]) > DET_NMS_THRESH for kb in keep):
                continue
            keep.append(b)
        return keep

    @staticmethod
    def _iou(a, b):
        ax1, ay1, ax2, ay2 = a
        bx1, by1, bx2, by2 = b
        ix1, iy1 = max(ax1, bx1), max(ay1, by1)
        ix2, iy2 = min(ax2, bx2), min(ay2, by2)
        iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
        inter = iw * ih
        ua = (ax2 - ax1) * (ay2 - ay1) + (bx2 - bx1) * (by2 - by1) - inter
        return inter / ua if ua > 0 else 0.0

    # ---------- 对齐 ----------
    def _align(self, img_bgr: np.ndarray, box: dict) -> Optional[np.ndarray]:
        cv2 = self._cv2
        kps = np.array(box["kps"], dtype=np.float32)
        if kps.shape != (5, 2) or not np.all(np.abs(kps) > 0):
            # 无关键点：按框中心裁剪（外扩 10%）
            x1, y1, x2, y2 = [max(0.0, v) for v in box["box"]]
            w, h = x2 - x1, y2 - y1
            pad_x, pad_y = w * 0.1, h * 0.1
            x1, y1 = max(0, int(x1 - pad_x)), max(0, int(y1 - pad_y))
            x2, y2 = min(img_bgr.shape[1], int(x2 + pad_x)), min(img_bgr.shape[0], int(y2 + pad_y))
            crop = img_bgr[y1:y2, x1:x2]
            if crop.size == 0:
                return None
            return cv2.resize(crop, (112, 112), interpolation=cv2.INTER_LINEAR)
        # 5 点相似变换（Umeyama，与 edge-box EstimateSimilarity 一致）
        M, _ = cv2.estimateAffinePartial2D(kps, ARCFACE_DST, method=cv2.LMEDS)
        if M is None:
            return None
        return cv2.warpAffine(img_bgr, M, (112, 112), flags=cv2.INTER_LINEAR + cv2.WARP_INVERSE_MAP)

    # ---------- 特征 ----------
    def _extract_feature(self, aligned: np.ndarray) -> Optional[np.ndarray]:
        # ArcFace 预处理：BGR->RGB、(v-127.5)/128，NCHW
        rgb = self._cv2.cvtColor(aligned, self._cv2.COLOR_BGR2RGB)
        blob = (rgb.transpose(2, 0, 1).astype(np.float32) - 127.5) / 128.0
        blob = np.expand_dims(blob, axis=0)
        out = self.rec_session.run(None, {self.rec_input: blob})[0][0]
        return l2_normalize(out.astype(np.float32))

    def extract(self, image_bytes: bytes) -> Optional[Tuple[np.ndarray, int]]:
        """返回 (特征, 人脸数)；未检测到人脸返回 None。"""
        import cv2

        arr = np.frombuffer(image_bytes, dtype=np.uint8)
        img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if img is None:
            return None
        boxes = self._detect(img)
        if not boxes:
            return None
        aligned = self._align(img, boxes[0])
        if aligned is None:
            return None
        feat = self._extract_feature(aligned)
        if feat is None or feat.shape[0] != DIM:
            return None
        return feat, len(boxes)


def create_engine() -> "MockEngine | OnnxEngine":
    if os.getenv("FACE_SERVICE_MOCK", "").strip() in ("1", "true", "yes"):
        return MockEngine()
    base = os.path.dirname(os.path.abspath(__file__))
    models_dir = os.path.join(base, "models")
    det_model = os.getenv("FACE_SERVICE_DET_MODEL") or os.path.join(models_dir, "det_10g.onnx")
    rec_model = os.getenv("FACE_SERVICE_REC_MODEL") or os.path.join(models_dir, "w600k_r50.onnx")
    if os.path.exists(det_model) and os.path.exists(rec_model):
        try:
            return OnnxEngine(det_model, rec_model)
        except Exception as e:  # noqa: BLE001
            print(f"[face-service] ONNX 加载失败，回退 Mock：{e}")
            return MockEngine()
    print(
        "[face-service] 未找到模型文件（models/det_10g.onnx, models/w600k_r50.onnx），"
        "回退 Mock 引擎。可通过 FACE_SERVICE_DET_MODEL / FACE_SERVICE_REC_MODEL 指定。"
    )
    return MockEngine()


def _selftest():
    e = create_engine()
    # 1x1 红色占位图
    import struct as _struct

    png = (
        b"\x89PNG\r\n\x1a\n"
        + b"\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90wS\xde"
        + b"\x00\x00\x00\x0cIDATx\x9cc\xf8\xff\xff?\x00\x05\xfe\x02\xfe\xa7\xb5\x8b\xf8\x00\x00\x00\x00IEND\xaeB`\x82"
    )
    r = e.extract(png)
    print(f"engine={e.name} selftest={'OK' if r else 'NO_FACE'}")


if __name__ == "__main__":
    _selftest()