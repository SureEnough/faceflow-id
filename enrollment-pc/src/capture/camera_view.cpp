// enrollment-pc/src/capture/camera_view.cpp
#include "capture/camera_view.h"

#include <QMediaDevices>
#include <QPixmap>
#include <QVBoxLayout>

namespace enroll {

CameraView::CameraView(QWidget* parent) : QWidget(parent) {
  viewer_ = new QVideoWidget(this);
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(viewer_);
}

bool CameraView::Start(const QString& deviceName) {
  QCameraDevice device;
  if (!deviceName.isEmpty()) {
    const auto cams = QMediaDevices::videoInputs();
    for (const auto& c : cams) {
      if (c.description() == deviceName) {
        device = c;
        break;
      }
    }
    if (device.isNull()) return false;
  }
  delete camera_;
  camera_ = new QCamera(device, this);
  session_.setCamera(camera_);
  session_.setVideoOutput(viewer_);
  camera_->start();
  return true;
}

void CameraView::Stop() {
  if (camera_) camera_->stop();
}

bool CameraView::GrabFrame(QImage& out) {
  if (!camera_ || camera_->active() == false) return false;
  // 骨架：使用 QScreen 抓取 viewer 当前画面（生产建议 QVideoSink 取原始帧）
  QWidget* w = viewer_;
  QPixmap pm = w->grab();
  out = pm.toImage().convertToFormat(QImage::Format_RGB888);
  return !out.isNull();
}

}  // namespace enroll