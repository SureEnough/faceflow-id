// edge-box/src/report/device_ops.h
// 设备侧与后台的运维交互：
// - EnsureDeviceRegistered：自注册主设备 + 代注册子摄像头（幂等，后台 upsert）
// - UpdateDeviceInfo：周期刷新在线状态（编辑设备接口，含子设备在线状态，后台设备树据此展示）
// - PullRemoteConfig：拉取后台下发的设备配置（远程配置优先）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/config.h"
#include "report/api_client.h"
#include "web/config_manager.h"

namespace eb {

// 确保设备已注册（device_id>0 直接使用；=0 时按 store_id/device_name 自注册）。
// 同时把摄像头代注册为子设备（type=3, key=camera_id）。返回生效的 device_id；失败返回原值。
int64_t EnsureDeviceRegistered(const Config& cfg, ApiClient* api, int64_t device_id,
                               std::string* err = nullptr);

// 周期刷新在线状态：PUT /devices/:id（编辑设备接口，后台据此更新"最后在线时间/在线状态"）。
// body = status=1 + 子设备在线状态；401 时重登重试。
bool UpdateDeviceInfo(int64_t device_id, ApiClient* api,
                      const std::vector<web::CameraStatus>& cams);

// 拉取后台配置：成功且 data.config 为对象时返回 true 并把 JSON 原样放入 out_json
bool PullRemoteConfig(int64_t device_id, ApiClient* api, std::string& out_json);

}  // namespace eb