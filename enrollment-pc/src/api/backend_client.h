// enrollment-pc/src/api/backend_client.h
// 后台 REST 客户端（Qt Network）：设备注册/登录、人员录入、核验上报、历史回查。
// 与 admin-backend 的 /api/v1 契约一致（见 docs/architecture_design.md 13 章）。
#pragma once

#include <functional>

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "verify/verify_logic.h"

namespace enroll {

class BackendClient : public QObject {
  Q_OBJECT
 public:
  explicit BackendClient(QString baseUrl, QString psk, QObject* parent = nullptr);

  // 设备注册（device_type=2 录入电脑端），成功后保存 device_id
  void RegisterDevice(const QString& name, quint64 storeId);
  // 设备登录（POST /auth/device/login），成功后保存 token
  void Login();
  // 创建人员 + 特征（POST /customers；内部人员 person_type=1 需 staff_no）
  void CreateCustomer(quint64 customerIdHint, int personType, const QString& name,
                      const QString& idCardNo, const QString& staffNo,
                      const Feature& feature);
  // 上报人证核验记录（POST /records/verify）
  void PostVerify(quint64 customerId, float similarity, bool passed, const QString& cameraId);
  // 历史来访回查（POST /history/search）
  void SearchHistory(const Feature& feature);

  quint64 deviceId() const { return device_id_; }
  bool hasToken() const { return !token_.isEmpty(); }

 signals:
  void deviceRegistered(quint64 deviceId);
  void loggedIn();
  void customerCreated(QJsonObject data);
  void verifyPosted();
  void historyResult(QJsonObject data);
  void errorOccurred(const QString& message);

 private:
  void request(const QString& method, const QString& path, const QJsonObject& payload,
               std::function<void(const QJsonObject&)> onOk);
  void setAuth(QNetworkRequest& req) const;
  static QString base64Feature(const Feature& f);

  QNetworkAccessManager net_;
  QString base_url_;
  QString psk_;
  QString token_;
  quint64 device_id_ = 0;
};

}  // namespace enroll