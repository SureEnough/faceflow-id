// enrollment-pc/src/api/backend_client.cpp
#include "api/backend_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <cstring>

namespace enroll {

BackendClient::BackendClient(QString baseUrl, QString psk, QObject* parent)
    : QObject(parent), base_url_(std::move(baseUrl)), psk_(std::move(psk)) {}

void BackendClient::setAuth(QNetworkRequest& req) const {
  req.setRawHeader("Content-Type", "application/json");
  if (!token_.isEmpty()) {
    req.setRawHeader("Authorization", ("Bearer " + token_).toUtf8());
  }
}

QString BackendClient::base64Feature(const Feature& f) {
  QByteArray raw(reinterpret_cast<const char*>(f.data()),
                 static_cast<int>(kFeatureDim * sizeof(float)));
  return QString::fromLatin1(raw.toBase64());
}

void BackendClient::request(const QString& method, const QString& path, const QJsonObject& payload,
                            std::function<void(const QJsonObject&)> onOk) {
  QNetworkRequest req{QUrl(base_url_ + path)};
  setAuth(req);
  QNetworkReply* reply = nullptr;
  QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if (method == "POST") {
    reply = net_.post(req, body);
  } else {
    reply = net_.get(req);
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, onOk]() {
    reply->deleteLater();
    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
      emit errorOccurred(reply->errorString());
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    if (root.value("code").toInt() != 0) {
      emit errorOccurred(root.value("message").toString());
      return;
    }
    onOk(root.value("data").toObject());
  });
}

void BackendClient::RegisterDevice(const QString& name, quint64 storeId) {
  QJsonObject payload{
      {"device_type", 2},
      {"name", name},
      {"store_id", static_cast<double>(storeId)},
      {"psk", psk_},
  };
  request("POST", "/devices/register", payload, [this](const QJsonObject& data) {
    device_id_ = static_cast<quint64>(data.value("device_id").toDouble());
    emit deviceRegistered(device_id_);
  });
}

void BackendClient::Login() {
  QJsonObject payload{{"device_id", static_cast<double>(device_id_)}, {"psk", psk_}};
  request("POST", "/auth/device/login", payload, [this](const QJsonObject& data) {
    token_ = data.value("token").toString();
    emit loggedIn();
  });
}

void BackendClient::CreateCustomer(quint64 /*customerIdHint*/, int personType,
                                   const QString& name, const QString& idCardNo,
                                   const QString& staffNo, const Feature& feature) {
  QJsonObject payload{
      {"person_type", personType},
      {"name", name},
      {"id_card_no", idCardNo},
      {"staff_no", staffNo},
      {"department", ""},
      {"gender", 0},
      {"birth_date", ""},
      {"address", ""},
      {"id_photo", ""},
      {"live_photo", ""},
      {"face_feature", base64Feature(feature)},
  };
  request("POST", "/customers", payload, [this](const QJsonObject& data) {
    emit customerCreated(data);
  });
}

void BackendClient::PostVerify(quint64 customerId, float similarity, bool passed,
                               const QString& cameraId) {
  QJsonObject payload{
      {"customer_id", static_cast<double>(customerId)},
      {"similarity", static_cast<double>(similarity)},
      {"passed", passed},
      {"camera_id", cameraId},
      {"verify_type", "idcard"},
  };
  request("POST", "/records/verify", payload, [this](const QJsonObject&) { emit verifyPosted(); });
}

void BackendClient::SearchHistory(const Feature& feature) {
  QJsonObject payload{{"feature", base64Feature(feature)}};
  request("POST", "/history/search", payload, [this](const QJsonObject& data) {
    emit historyResult(data);
  });
}

}  // namespace enroll