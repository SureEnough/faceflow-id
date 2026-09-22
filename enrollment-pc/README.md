# enrollment-pc 顾客录入电脑端（C++ / Qt）

FaceFlow 顾客录入电脑端：读卡器读取身份证 → USB 摄像头现场拍照 → 1:1 人证比对 → 录入人员库 / 历史来访回查。

## 目录结构（架构文档 4.2.3）

```
enrollment-pc/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                    # 入口（QApplication + MainWindow）
│   ├── ui/MainWindow.{h,cpp}       # 主窗口：注册/读卡/拍照比对/录入
│   ├── idcard/
│   │   ├── idcard_reader.h         # 读卡器抽象接口（IIdCardReader）
│   │   ├── idcard_simulator.{h,cpp}# 模拟器（开发/演示）
│   │   └── (huawei/jinglun 适配待接入厂商 SDK)
│   ├── capture/camera_view.{h,cpp} # USB 摄像头预览与抓帧（Qt Multimedia）
│   ├── verify/verify_logic.{h,cpp} # 1:1 余弦比对 + 阈值判定（纯 C++）
│   └── api/backend_client.{h,cpp}  # 后台 REST 客户端（Qt Network）
```

## 构建

```bash
# 依赖：Qt6 Widgets/Network/Multimedia（Windows / Ubuntu 均可）
cmake -B build -DQt6_DIR=<qt6-cmake-dir>
cmake --build build -j4
```

## 运行

```bash
./build/enrollment_app
# 启动后：注册设备 → 读身份证（模拟器自动返回演示卡）→ 现场拍照比对 → 录入
# 后台需先在 127.0.0.1:8080 启动 admin-backend（DEVICE_PSK 需与录入端一致）
```

## 实现状态

- ✅ 读卡器硬件抽象层（IIdCardReader + 模拟器），厂商 SDK 待接入
- ✅ 1:1 人证比对逻辑（verify_logic，纯 C++ 可单测）
- ✅ 后台 REST 客户端（设备注册/登录、人员录入、核验上报、历史回查）
- ✅ Qt 主窗口骨架（读卡 → 抓帧 → 比对 → 录入）
- ⏳ 特征提取：接 ONNXRuntime（SCRFD + ArcFace）提取现场/证件照特征（当前为演示占位）
- ⏳ 活体检测接入（防照片攻击，文档 5.2）
- ⏳ 华视 CVR-100U / 精伦 IDR210 厂商 SDK 适配