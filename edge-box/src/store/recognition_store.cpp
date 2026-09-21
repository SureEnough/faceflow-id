#include "store/recognition_store.h"

#include <algorithm>
#include <ctime>
#include <deque>

namespace eb {

// 内存实现（无 SQLite 依赖：开发自测/CI）
class MemStore : public RecognitionStore {
 public:
  bool Insert(const Recognition& rec) override {
    items_.push_back(StoredRecognition{rec, 0});
    return true;
  }

  std::vector<StoredRecognition> Pending(int limit) override {
    std::vector<StoredRecognition> out;
    for (auto& it : items_) {
      if (it.sync_status == 0) {
        out.push_back(it);
        if (static_cast<int>(out.size()) >= limit) break;
      }
    }
    return out;
  }

  void MarkConfirmed(const Recognition& rec) override {
    for (auto& it : items_) {
      if (it.rec.track_id == rec.track_id && it.rec.camera_id == rec.camera_id &&
          it.rec.created_at == rec.created_at) {
        it.sync_status = 1;
        return;
      }
    }
  }

  int64_t Cleanup(int64_t now_unix, int retention_days) override {
    int64_t before = static_cast<int64_t>(items_.size());
    int64_t cutoff = now_unix - static_cast<int64_t>(retention_days) * 24 * 3600;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [cutoff](const StoredRecognition& it) { return it.rec.created_at < cutoff; }),
                 items_.end());
    return before - static_cast<int64_t>(items_.size());
  }

 private:
  std::deque<StoredRecognition> items_;
};

#if defined(HAVE_SQLITE3)
// SQLite 实现（真实设备）：建表 recognition_logs(device_id, track_id, camera_id, ... , sync_status)
class SQLiteStore : public RecognitionStore {
 public:
  explicit SQLiteStore(const std::string& path) : path_(path) {}
  bool Insert(const Recognition& rec) override;
  std::vector<StoredRecognition> Pending(int limit) override;
  void MarkConfirmed(const Recognition& rec) override;
  int64_t Cleanup(int64_t now_unix, int retention_days) override;

 private:
  std::string path_;
  // 生产实现：sqlite3_open / prepared statements；字段与后台契约幂等键一致
};
#endif

RecognitionStore* CreateRecognitionStore() {
#if defined(HAVE_SQLITE3)
  return new SQLiteStore("edge_box.db");
#else
  return new MemStore();
#endif
}

}  // namespace eb