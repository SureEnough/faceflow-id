// enrollment-pc/src/ui/MainWindow.cpp
#include "ui/MainWindow.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>

#include <cmath>

namespace enroll {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("FaceFlow 顾客录入端"));
  resize(960, 600);

  // 后台地址与设备 PSK 从环境变量读取（生产禁止硬编码；未设置时回退开发默认值）
  const QString apiUrl = qEnvironmentVariable("ENROLL_API_URL", "http://127.0.0.1:8080/api/v1");
  const QString devicePsk = qEnvironmentVariable("ENROLL_PSK", "dev-psk-change-me");
  api_ = new BackendClient(apiUrl, devicePsk, this);
  reader_ = CreateIdCardReader("simulator");  // 生产替换为 huawei/jinglun
  camera_ = new CameraView(this);

  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  // 顶部操作区
  auto* ops = new QHBoxLayout;
  auto* btn_reg = new QPushButton(QStringLiteral("注册设备"), central);
  btn_read_ = new QPushButton(QStringLiteral("读身份证"), central);
  btn_capture_ = new QPushButton(QStringLiteral("现场拍照并比对"), central);
  btn_enroll_ = new QPushButton(QStringLiteral("录入人员库"), central);
  btn_enroll_->setEnabled(false);
  ops->addWidget(btn_reg);
  ops->addWidget(btn_read_);
  ops->addWidget(btn_capture_);
  ops->addWidget(btn_enroll_);
  ops->addStretch(1);

  // 中部：摄像机预览 + 状态
  auto* mid = new QHBoxLayout;
  mid->addWidget(camera_, 1);
  status_ = new QLabel(QStringLiteral("未连接读卡器"), central);
  status_->setMinimumWidth(220);
  mid->addWidget(status_);

  // 底部：日志
  log_ = new QTextEdit(central);
  log_->setReadOnly(true);
  log_->setMaximumHeight(160);

  root->addLayout(ops);
  root->addLayout(mid, 1);
  root->addWidget(log_);
  setCentralWidget(central);

  connect(btn_reg, &QPushButton::clicked, this, &MainWindow::OnRegister);
  connect(btn_read_, &QPushButton::clicked, this, &MainWindow::OnReadCard);
  connect(btn_capture_, &QPushButton::clicked, this, &MainWindow::OnCaptureAndVerify);
  connect(btn_enroll_, &QPushButton::clicked, this, &MainWindow::OnEnroll);
  connect(api_, &BackendClient::loggedIn, this, [this]() { Log(QStringLiteral("设备登录成功")); });
  connect(api_, &BackendClient::errorOccurred, this, [this](const QString& m) { Log(QStringLiteral("错误: ") + m); });

  reader_->Open();
  camera_->Start();
  Log(QStringLiteral("录入端就绪（读卡器模拟器）"));
}

void MainWindow::Log(const QString& s) {
  log_->append(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + s);
}

void MainWindow::OnRegister() {
  api_->RegisterDevice(QStringLiteral("录入电脑-01"), 1);
}

void MainWindow::OnReadCard() {
  IdCardInfo card;
  const ReaderError err = reader_->Read(card);
  if (err != ReaderError::kOk) {
    status_->setText(QStringLiteral("读卡失败"));
    Log(QStringLiteral("读卡失败: err=%1").arg(static_cast<int>(err)));
    return;
  }
  card_ = card;
  has_card_ = true;
  status_->setText(QStringLiteral("已读卡: %1 %2").arg(card_.name, card_.id_card_no));
  Log(QStringLiteral("读卡成功: %1 %2").arg(card_.name, card_.id_card_no));
}

void MainWindow::OnCaptureAndVerify() {
  if (!has_card_) {
    QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先读取身份证"));
    return;
  }
  QImage frame;
  if (!camera_->GrabFrame(frame)) {
    Log(QStringLiteral("抓帧失败"));
    return;
  }
  // 骨架：现场特征用演示值（生产：SCRFD 检测 + ArcFace 提取现场特征，
  // 与证件照特征做 1:1 余弦比对，文档 15.4）
  live_feature_ = DemoFeature(static_cast<quint64>(frame.width() * 31 + frame.height()));

  // 证件照特征：骨架同样用演示值（生产：读卡器 SDK 取证件照后 ArcFace 提特征）
  const Feature id_feature = DemoFeature(42);
  const float sim = CosineSimilarity(live_feature_, id_feature);
  const bool pass = VerifyPass(live_feature_, id_feature, 0.50f);
  status_->setText(pass ? QStringLiteral("人证比对通过 sim=%1").arg(sim, 0, 'f', 3)
                        : QStringLiteral("人证比对不通过 sim=%1").arg(sim, 0, 'f', 3));
  Log(QStringLiteral("现场特征已提取，与证件照相似度=%1 -> %2")
          .arg(sim, 0, 'f', 3)
          .arg(pass ? QStringLiteral("通过") : QStringLiteral("不通过")));
  if (pass) {
    api_->PostVerify(0, sim, true, QStringLiteral("usb-cam-01"));
    btn_enroll_->setEnabled(true);
  }
}

void MainWindow::OnEnroll() {
  if (!has_card_) return;
  // 骨架：演示特征入库（customerIdHint 由后台返回；真实录入完成后回查历史）
  api_->CreateCustomer(0, 0, card_.name, card_.id_card_no, QString(), live_feature_);
  Log(QStringLiteral("已提交录入: %1").arg(card_.name));
  btn_enroll_->setEnabled(false);
}

Feature MainWindow::DemoFeature(quint64 seed) const {
  Feature f{};
  // 确定性占位特征（L2 归一化）；生产替换为 ArcFace 输出
  uint64_t s = seed ? seed : 1;
  double norm = 0;
  for (size_t i = 0; i < kFeatureDim; ++i) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    f[i] = static_cast<float>((s >> 33) & 0xFFFF) / 65535.0f - 0.5f;
    norm += static_cast<double>(f[i]) * f[i];
  }
  norm = std::sqrt(norm);
  if (norm > 1e-9) {
    for (float& v : f) v = static_cast<float>(v / norm);
  }
  return f;
}

}  // namespace enroll