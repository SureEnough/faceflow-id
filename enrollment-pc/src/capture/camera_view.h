// enrollment-pc/src/capture/camera_view.h
// 摄像头预览控件（Qt Multimedia）：USB 摄像头预览 + 抓一帧（供特征提取）。
// 骨架：生产环境需接入 ArcFace/SCRFD 提取现场特征。
#pragma once

#include <QCamera>
#include <QCameraImageCapture>
#include <QImage>
#include <QMediaCaptureSession>
#include <QVideoWidget>
#include <QWidget>

namespace enroll {

class CameraView : public QWidget {
  Q_OBJECT
 public:
  explicit CameraView(QWidget* parent = nullptr);

  bool Start(const QString& deviceName = "");
  void Stop();
  // 抓取当前帧；未就绪返回 false
  bool GrabFrame(QImage& out);

 private:
  QCamera* camera_ = nullptr;
  QMediaCaptureSession session_;
  QVideoWidget* viewer_ = nullptr;
  QCameraImageCapture* capture_ = nullptr;
};

}  // namespace enroll