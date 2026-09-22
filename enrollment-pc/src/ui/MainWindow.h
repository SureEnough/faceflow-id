// enrollment-pc/src/ui/MainWindow.h
// 录入电脑端主窗口：读卡 → 现场照 → 1:1 人证比对 → 录入/回查。
#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>

#include "api/backend_client.h"
#include "capture/camera_view.h"
#include "idcard/idcard_reader.h"
#include "verify/verify_logic.h"

namespace enroll {

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private slots:
  void OnRegister();
  void OnReadCard();
  void OnCaptureAndVerify();
  void OnEnroll();

 private:
  // 骨架：生产环境接 ONNXRuntime（SCRFD/ArcFace），此处生成确定性演示特征
  Feature DemoFeature(quint64 seed) const;

  BackendClient* api_;
  CameraView* camera_;
  IIdCardReader* reader_;
  IdCardInfo card_;
  bool has_card_ = false;
  Feature live_feature_{};

  QLabel* status_;
  QTextEdit* log_;
  QPushButton* btn_read_;
  QPushButton* btn_capture_;
  QPushButton* btn_enroll_;
  void Log(const QString& s);
};

}  // namespace enroll