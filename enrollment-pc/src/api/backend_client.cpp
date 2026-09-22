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

void BackendClient::requestImpl(const QString& method, const QString& path, const QJsonObject& payload,
                             std::function<void(const QJsonObject&)> onOk, int attempt, bool allowRelogin) {
  QNetworkRequest req{QUrl(base_url_ + path)};
  setAuth(req);
  QNetworkReply* reply = nullptr;
  QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if (method == "POST") {
    reply = net_.post(req, body);
  } else {
    reply = net_.get(req);
  }
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, method, path, payload, onOk, attempt, allowRelogin]() {
    reply->deleteLater();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = reply->readAll();

    // token 失效（401）：自动重登一次并重放原请求（非登录请求才允许）
    if (httpStatus == 401 && allowRelogin && attempt < 1) {
      loginWithCallback([this, method, path, payload, onOk](bool ok) {
        if (!ok) {
          emit errorOccurred(QStringLiteral("令牌失效且重新登录失败，请检查设备 PSK"));
          return;
        }
        requestImpl(method, path, payload, onOk, /*attempt=*/1, /*allowRelogin=*/true);
      });
      return;
    }
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
  requestImpl("POST", "/devices/register", payload, [this](const QJsonObject& data) {
    device_id_ = static_cast<quint64>(data.value("device_id").toDouble());
    emit deviceRegistered(device_id_);
  });
}

void BackendClient::Login(std::function<void(bool)> onDone) {
  loginWithCallback(std::move(onDone));
}

void BackendClient::loginWithCallback(std::function<void(bool)> onDone) {
  QJsonObject payload{{"device_id", static_cast<double>(device_id_)}, {"psk", psk_}};
  // 登录请求本身 401 不触发重登，避免死循环（psk 错误直接失败）
  requestImpl("POST", "/auth/device/login", payload,
              [this, onDone](const QJsonObject& data) {
                token_ = data.value("token").toString();
                emit loggedIn();
                if (onDone) onDone(true);
              },
              /*attempt=*/0, /*allowRelogin=*/false);
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
  requestImpl("POST", "/customers", payload, [this](const QJsonObject& data) {
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
  requestImpl("POST", "/records/verify", payload, [this](const QJsonObject&) { emit verifyPosted(); });
}

void BackendClient::SearchHistory(const Feature& feature) {
  QJsonObject payload{{"feature", base64Feature(feature)}};
  requestImpl("POST", "/history/search", payload, [this](const QJsonObject& data) {
    emit historyResult(data);
  });
}

}  // namespace enroll