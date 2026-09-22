// edge-box/src/store/recognition_store.h
// 识别记录本地存储（断网缓存，sync_status 标记待上报）。
#pragma once

#include <vector>

#include "common/common.h"

namespace eb {

// 本地记录（含同步状态）
struct StoredRecognition {
  Recognition rec;
  int sync_status = 0;  // 0 待上报 / 1 已上报
};

class RecognitionStore {
 public:
  virtual ~RecognitionStore() = default;

  // 插入一条（匿名轨迹也会入库，特征 + 抓拍 + 时间）
  virtual bool Insert(const Recognition& rec) = 0;

  // 取出待上报记录并标记为已上报（上报成功后调用 MarkConfirmed）
  virtual std::vector<StoredRecognition> Pending(int limit) = 0;

  // 取最近 N 条记录（按时间倒序；Web 端“最近抓拍”用）
  virtual std::vector<StoredRecognition> Recent(int limit) = 0;

  // 将待上报记录标记成功（按 track_id + camera_id + created_at）
  virtual void MarkConfirmed(const Recognition& rec) = 0;

  // 清理超过 retention_days（Unix 秒）的记录
  virtual int64_t Cleanup(int64_t now_unix, int retention_days) = 0;
};

// 创建本地存储：有 SQLite 时用 SQLiteStore（edge_box.db），否则内存实现（自测/CI）
RecognitionStore* CreateRecognitionStore();
// 强制创建内存实现（测试用）
RecognitionStore* CreateMemStore();

}  // namespace eb