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

// 识别记录查询条件（Web 端"识别记录"筛选；空字段=不限制）
struct RecognitionQuery {
  int limit = 20;
  std::string camera_id;  // 相机 ID，空=全部
  int identified = -1;    // -1 全部 / 1 仅命中档案 / 0 仅匿名
  int person_type = -1;   // -1 全部 / 0 顾客 / 1 员工
  int direction = -1;     // -1 全部 / 0 进 / 1 出
  double min_similarity = 0;  // 相似度下限
  int64_t start_at = 0;   // Unix 秒，0=不限
  int64_t end_at = 0;     // Unix 秒，0=不限
};

class RecognitionStore {
 public:
  virtual ~RecognitionStore() = default;

  // 插入一条（匿名轨迹也会入库，特征 + 抓拍 + 时间）
  virtual bool Insert(const Recognition& rec) = 0;

  // 取出待上报记录并标记为已上报（上报成功后调用 MarkConfirmed）
  virtual std::vector<StoredRecognition> Pending(int limit) = 0;

  // 取最近 N 条记录（按时间倒序；兼容旧接口）
  virtual std::vector<StoredRecognition> Recent(int limit) = 0;

  // 按条件查询最近记录（Web 端"识别记录"用；按时间倒序，limit 上限）
  virtual std::vector<StoredRecognition> RecentFiltered(const RecognitionQuery& q) = 0;

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