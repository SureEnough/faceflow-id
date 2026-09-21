#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一键演示数据脚本：注册设备 → 上报模拟识别记录（匿名轨迹） → 顾客录入触发历史回查
用法: python3 scripts/demo_seed.py [base_url]
"""
import base64
import json
import struct
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080/api/v1"
ADMIN_USER = "admin"
ADMIN_PASS = "admin123"
TOKEN = ""
DIM = 512


def post(path, payload):
    req = urllib.request.Request(
        BASE + path,
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + TOKEN},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode())


def get(path):
    req = urllib.request.Request(BASE + path, headers={"Authorization": "Bearer " + TOKEN})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode())


def feature(seed):
    """确定性 512 维 float32 特征（little-endian，与 Go 一致）"""
    s = seed or 1
    vals = []
    for _ in range(DIM):
        s = (1103515245 * s + 12345) & 0x7FFFFFFF
        vals.append((s / 0x7FFFFFFF) * 2 - 1)
    return base64.b64encode(struct.pack("<%df" % DIM, *vals)).decode()


def iso(days_ago, hhmm="10:30:00"):
    dt = datetime.now(timezone.utc) - timedelta(days=days_ago)
    ymd = dt.strftime("%Y-%m-%d")
    return f"{ymd}T{hhmm}Z"


def main():
    try:
        health = get("/health")
        print("[1/6] 后端在线:", health["data"]["status"])
    except Exception as e:
        print("ERROR: 后端不可用，请先启动: cd admin-backend && ./bin/server", e)
        sys.exit(1)

    # JWT 登录
    global TOKEN
    data = post("/auth/login", {"username": ADMIN_USER, "password": ADMIN_PASS})
    TOKEN = data["data"]["token"]
    print("[1/6] 登录成功:", data["data"]["user"])

    # 特征：顾客A(张三) / 顾客B(李四) / 员工(E1024)
    feat_a, feat_b, feat_c = feature(42), feature(7), feature(99)

    # [2/6] 注册设备（幂等）
    edge = post("/devices/register", {"device_type": 1, "name": "边缘盒子-01", "store_id": 1, "psk": "dev-psk-change-me"})["data"]["device_id"]
    post("/devices/register", {"device_type": 3, "parent_id": edge, "device_key": "cam-01", "name": "RTSP摄像头-入口", "store_id": 1, "psk": "dev-psk-change-me"})
    pc = post("/devices/register", {"device_type": 2, "name": "录入电脑-01", "store_id": 1, "psk": "dev-psk-change-me"})["data"]["device_id"]
    post("/devices/register", {"device_type": 5, "parent_id": pc, "device_key": "reader-01", "name": "身份证读卡器", "store_id": 1, "psk": "dev-psk-change-me"})
    print(f"[2/6] 设备注册完成: 边缘盒={edge} 录入端={pc} (+子设备)")

    # [3/6] 上报历史识别记录（匿名轨迹）
    records = []
    # 顾客A 来店 3 次（不同天）
    for i, days in enumerate([10, 5, 1]):
        records.append({"track_id": f"A-{i}", "person_type": 0, "face_feature": feat_a,
                        "direction": 0, "camera_id": "cam-01", "created_at": iso(days)})
    # 顾客B 来店 1 次
    records.append({"track_id": "B-0", "person_type": 0, "face_feature": feat_b,
                    "direction": 0, "camera_id": "cam-01", "created_at": iso(3)})
    r = post("/records/recognition/batch", {"device_id": edge, "records": records})
    print(f"[3/6] 上报识别记录: accepted={r['data']['accepted']}")

    # [4/6] 顾客张三 录入 → 触发历史回查（应统计出 3 次）
    zhang = post("/customers", {"person_type": 0, "name": "张三", "id_card_no": "110101199001011234",
                                "birth_date": "1990-01-01", "address": "北京市朝阳区xx",
                                "face_feature": feat_a})
    h = zhang["data"]["history"]
    print(f"[4/6] 张三录入: customer_id={zhang['data']['customer_id']} -> 历史来访 {h['total_visits']}次/{h['visit_days']}天, 最近{h['last_visit_at']}")

    # 顾客李四 录入
    li = post("/customers", {"person_type": 0, "name": "李四", "id_card_no": "110101199003031234",
                             "face_feature": feat_b})
    h2 = li["data"]["history"]
    print(f"[4/6] 李四录入: customer_id={li['data']['customer_id']} -> 历史来访 {h2['total_visits']}次")

    # [5/6] 内部人员录入
    staff = post("/customers", {"person_type": 1, "name": "王经理", "staff_no": "E1024",
                                "department": "运营部", "face_feature": feat_c})
    staff_id = staff["data"]["customer_id"]
    # 员工识别记录（模拟边缘盒识别命中后回填 customer_id）
    post("/records/recognition/batch", {"device_id": edge, "records": [
        {"track_id": "C-in", "person_type": 1, "customer_id": staff_id, "face_feature": feat_c,
         "direction": 0, "camera_id": "cam-01", "created_at": iso(1, "09:00:00")},
        {"track_id": "C-out", "person_type": 1, "customer_id": staff_id, "face_feature": feat_c,
         "direction": 1, "camera_id": "cam-01", "created_at": iso(1, "18:00:00")},
    ]})
    print(f"[5/6] 内部人员录入: customer_id={staff_id} (E1024) + 上报员工进出记录")

    # [6/6] 统计汇总
    start = (datetime.now(timezone.utc) - timedelta(days=30)).strftime("%Y-%m-%dT00:00:00Z")
    flow = get(f"/stats/flow?granularity=day&start_at={urllib.parse.quote(start)}")
    staff_stats = get(f"/stats/staff?start_at={urllib.parse.quote(start)}")
    print(f"[6/6] 顾客客流总计: 进={flow['data']['total']['in']} 出={flow['data']['total']['out']}")
    for it in staff_stats["data"]["items"]:
        print(f"      员工 {it['staff_no']} ({it['department']}): 进={it['in']} 出={it['out']}")

    print("\n✅ 演示数据就绪！打开前端 http://localhost:5173/ 查看设备树/人员库/客流统计；")
    print("   历史来访回查页点「🎲 生成测试特征」即可另测新身份。")


if __name__ == "__main__":
    main()